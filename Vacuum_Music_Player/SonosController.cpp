#include "SonosController.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <future>
#include <sstream>
#include <iomanip>
#include <thread>
#include <unordered_set>

// noson 库头文件
#include <sonossystem.h>
#include <sonosplayer.h>
// cpp-httplib（header-only）
#include <httplib.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// ================= 工具函数 =================
namespace {
    std::string PathToUTF8(const std::string& localPath) {
        std::filesystem::path p(localPath);
        std::wstring wstr = p.wstring();
        if (wstr.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::vector<char> buf(len);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, buf.data(), len, nullptr, nullptr);
        return std::string(buf.data());
    }

    std::string URLEncode(const std::string& str) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        for (char c : str) {
            if (std::isalnum(static_cast<unsigned char>(c)) ||
                c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            }
            else {
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
                escaped << std::nouppercase;
            }
        }
        return escaped.str();
    }

    std::string ToLower(const std::string& str) {
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return lower;
    }
}

// ================= 网卡评分 =================
namespace {
    struct AdapterInfo {
        std::string ip;
        std::string description;
        bool hasGateway;
        int priorityScore;
    };

    std::vector<AdapterInfo> GetPhysicalAdapters() {
        std::vector<AdapterInfo> result;
        ULONG size = 0;
        GetAdaptersInfo(nullptr, &size);
        std::vector<BYTE> buf(size);
        PIP_ADAPTER_INFO adapter = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
        if (GetAdaptersInfo(adapter, &size) != ERROR_SUCCESS)
            return result;

        for (PIP_ADAPTER_INFO p = adapter; p; p = p->Next) {
            std::string ip = p->IpAddressList.IpAddress.String;
            if (ip.empty() || ip == "0.0.0.0" || ip == "127.0.0.1")
                continue;

            std::string desc = p->Description;
            std::string lowerDesc = ToLower(desc);

            if (lowerDesc.find("virtual") != std::string::npos ||
                lowerDesc.find("hyper-v") != std::string::npos ||
                lowerDesc.find("vmware") != std::string::npos ||
                lowerDesc.find("virtualbox") != std::string::npos ||
                lowerDesc.find("wsl") != std::string::npos ||
                lowerDesc.find("docker") != std::string::npos ||
                lowerDesc.find("loopback") != std::string::npos ||
                lowerDesc.find("bluetooth") != std::string::npos) {
                continue;
            }

            AdapterInfo info;
            info.ip = ip;
            info.description = desc;
            info.hasGateway = (strcmp(p->GatewayList.IpAddress.String, "0.0.0.0") != 0);

            info.priorityScore = 0;
            if (p->Type == MIB_IF_TYPE_ETHERNET) info.priorityScore += 10;
            else if (p->Type == IF_TYPE_IEEE80211) info.priorityScore += 5;

            if (lowerDesc.find("intel") != std::string::npos) info.priorityScore += 15;
            if (lowerDesc.find("gigabit") != std::string::npos ||
                lowerDesc.find("gbe") != std::string::npos ||
                lowerDesc.find("1000") != std::string::npos ||
                lowerDesc.find("2.5g") != std::string::npos ||
                lowerDesc.find("10g") != std::string::npos) info.priorityScore += 10;
            if (lowerDesc.find("pci") != std::string::npos) info.priorityScore += 5;
            if (lowerDesc.find("realtek") != std::string::npos ||
                lowerDesc.find("broadcom") != std::string::npos ||
                lowerDesc.find("qualcomm") != std::string::npos) info.priorityScore += 3;
            if (lowerDesc.find("killer") != std::string::npos ||
                lowerDesc.find("gaming") != std::string::npos) info.priorityScore += 5;
            if (info.hasGateway) info.priorityScore += 5;

            result.push_back(info);
        }

        std::sort(result.begin(), result.end(),
            [](const AdapterInfo& a, const AdapterInfo& b) {
                return a.priorityScore > b.priorityScore;
            });
        return result;
    }
}

// ================= SSDP 发现 =================
namespace {
    struct SonosDevice {
        std::string location;
        std::string server;
        std::string ip;
    };

    std::vector<SonosDevice> DiscoverSonosDevices(const std::string& localIP, int timeoutSec) {
        std::vector<SonosDevice> devices;
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) return devices;

        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        int ttl = 4;
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));

        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(0);
        inet_pton(AF_INET, localIP.c_str(), &local.sin_addr);
        if (bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
            closesocket(sock);
            return devices;
        }

        const char* query =
            "M-SEARCH * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\n"
            "MX: 2\r\n"
            "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
            "\r\n";

        sockaddr_in dest = {};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(1900);
        inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);

        if (sendto(sock, query, (int)strlen(query), 0, (sockaddr*)&dest, sizeof(dest)) == SOCKET_ERROR) {
            closesocket(sock);
            return devices;
        }

        fd_set readfds;
        timeval tv = { timeoutSec, 0 };
        char buf[4096];

        while (true) {
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            if (select(0, &readfds, nullptr, nullptr, &tv) <= 0)
                break;

            sockaddr_in from;
            int fromLen = sizeof(from);
            int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
            if (n <= 0) break;
            buf[n] = '\0';
            std::string response(buf);

            auto findHeader = [&](const std::string& header) -> std::string {
                size_t pos = response.find(header + ": ");
                if (pos == std::string::npos) return "";
                pos += header.size() + 2;
                size_t end = response.find("\r\n", pos);
                return response.substr(pos, end - pos);
                };

            std::string location = findHeader("LOCATION");
            if (!location.empty()) {
                bool exists = false;
                for (auto& d : devices) if (d.location == location) { exists = true; break; }
                if (!exists) {
                    SonosDevice dev;
                    dev.location = location;
                    dev.server = findHeader("SERVER");
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
                    dev.ip = ipStr;
                    devices.push_back(dev);
                }
            }
        }

        closesocket(sock);
        return devices;
    }
}

// ================= SonosController::Impl =================
struct SonosController::Impl {
    bool initialized = false;
    bool connected = false;
    std::unique_ptr<SONOS::System> sonosSystem;
    std::string localIP ="127.0.0.1";
    int serverPort =0;
    std::string musicPath;
    std::thread httpThread;
    httplib::Server* httpServer = nullptr;
    bool httpRunning = false;

    ~Impl() {
        Stop();
    }

    void Stop() {
        StopHttpServer();
        sonosSystem.reset();
        connected = false;
        if (initialized) {
            WSACleanup();
            initialized = false;
        }
    }

    void StopHttpServer() {
        if (httpRunning && httpServer) {
            httpServer->stop();
            if (httpThread.joinable()) httpThread.join();
            httpRunning = false;
            httpServer = nullptr;
        }
    }
    std::string GetLocalIP() {
        char hostname[256];
        gethostname(hostname, sizeof(hostname));
        struct addrinfo hints = {}, * info;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hostname, NULL, &hints, &info) != 0)
            return "127.0.0.1";

        char ipStr[INET_ADDRSTRLEN] = { 0 };
        if (info && info->ai_family == AF_INET) {
            sockaddr_in* addr = (sockaddr_in*)info->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ipStr, INET_ADDRSTRLEN);
        }
        freeaddrinfo(info);

        std::string ip = ipStr;
        if (ip == "127.0.0.1") {
            // fallback: 使用 UDP socket 获取出口 IP
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in dst = {};
            dst.sin_family = AF_INET;
            dst.sin_port = htons(53);
            inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);
            if (connect(s, (sockaddr*)&dst, sizeof(dst)) == 0) {
                sockaddr_in local;
                int len = sizeof(local);
                getsockname(s, (sockaddr*)&local, &len);
                inet_ntop(AF_INET, &local.sin_addr, ipStr, INET_ADDRSTRLEN);
                ip = ipStr;   // 直接赋值，长度自动正确
            }
            closesocket(s);
        }

        // 确保丢掉多余的空字符（其实 ipStr 复制进来时自动截断了）
        return ip.c_str();   // 或者直接 return ip;
    }
    bool StartHttpServer(const std::wstring& musicFolder, int port) {
        // 1. 检查文件夹（直接使用宽字符路径）
        if (!std::filesystem::exists(musicFolder)) {
            OutputDebugStringA("[StartHttpServer] FAIL: folder does not exist\n");
            return false;
        }

        // 2. 获取本地IP
        localIP = GetLocalIP();
        if (localIP.empty()) localIP = "127.0.0.1";

        // 3. 端口有效性
        serverPort = port;
        if (serverPort <= 0 || serverPort > 65535) {
            OutputDebugStringA("[StartHttpServer] FAIL: invalid port\n");
            return false;
        }

        // 4. 路径转为 UTF‑8（用于 httplib）
        int len = WideCharToMultiByte(CP_UTF8, 0, musicFolder.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) {
            OutputDebugStringA("[StartHttpServer] FAIL: WideCharToMultiByte error\n");
            return false;
        }
        std::string utf8Path(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, musicFolder.c_str(), -1, &utf8Path[0], len, nullptr, nullptr);
        musicPath = utf8Path;  // 保存 UTF‑8 版本，供 ListMusicFiles 等使用

        // 5. 启动 HTTP 线程
        httpRunning = true;
        httpThread = std::thread([this, utf8Path, port]() {
            httplib::Server svr;
            svr.set_mount_point("/", utf8Path.c_str());
            httpServer = &svr;
            if (!svr.listen("0.0.0.0", port)) {
                OutputDebugStringA("[StartHttpServer] FAIL: listen failed\n");
            }
            httpServer = nullptr;
            httpRunning = false;
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        OutputDebugStringA(("[StartHttpServer] SUCCESS, port=" + std::to_string(port) + "\n").c_str());
        return httpRunning;
    }

    NSROOT::PlayerPtr GetPlayer() {
        if (!connected || !sonosSystem)
            return NSROOT::PlayerPtr();   // 返回空的 PlayerPtr，而不是 nullptr
        auto zones = sonosSystem->GetZoneList();
        if (zones.empty())
            return NSROOT::PlayerPtr();
        return sonosSystem->GetPlayer(zones.begin()->second, 0, nullptr);
    }

    std::vector<std::string> ListMusicFiles() {
        std::vector<std::string> files;
        if (!std::filesystem::exists(musicPath)) return files;
        for (const auto& entry : std::filesystem::directory_iterator(musicPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".mp3" || ext == ".flac" || ext == ".wav" || ext == ".wma" || ext == ".m4a") {
                    std::wstring wname = entry.path().filename().wstring();
                    int len = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string utf8name(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, &utf8name[0], len, nullptr, nullptr);
                        files.push_back(utf8name);
                    }
                }
            }
        }
        return files;
    }

    bool PlayFile(const std::string& filename) {
        using namespace NSROOT;
        auto player = GetPlayer();
        if (!player) return false;
        player->RemoveAllTracksFromQueue();
        std::string encoded = URLEncode(filename);
        std::string url = "http://" + localIP + ":" + std::to_string(serverPort) + "/" + encoded;
        DigitalItemPtr item(new DigitalItem(DigitalItem::Type_item, DigitalItem::SubType_audioItem));
        item->SetProperty("dc:title", filename);
        item->SetProperty("res", url);
        item->SetProperty("upnp:class", "object.item.audioItem.musicTrack");
        OutputDebugStringA((url + "\n").c_str());
        if (player->SetCurrentURI(item))
        {
            return player->Play();
        }
    }
    int ParseRelTimeToMs(const std::string& relTime) {
        int h = 0, m = 0, s = 0, ms = 0;
        // 尝试解析带毫秒的格式
        if (sscanf(relTime.c_str(), "%d:%d:%d.%d", &h, &m, &s, &ms) >= 3) {
            return ((h * 3600 + m * 60 + s) * 1000) + ms;
        }
        return 0;
    }
    void SetPlayPos(int pos) {
        auto player = GetPlayer();
        if (player) {
            player->SeekTime(pos / 1000);
        }
    }
    int GetPosition() {
        auto player = GetPlayer();

        SONOS::ElementList vars;
        if (player->GetPositionInfo(vars)) {
            // 查找 RelTime 键
            const std::string& relTimeStr = vars.GetValue("RelTime");
            if (!relTimeStr.empty()) {
                int ms = ParseRelTimeToMs(relTimeStr);
                if (ms > 0) {
                    return ms;
                }
            }
        }
    }
    int GetSonosTrackDuration() {
        auto player =GetPlayer();
        if (!player) return 0;

        SONOS::ElementList vars;
        if (player->GetPositionInfo(vars)) {
            const std::string& durationStr = vars.GetValue("TrackDuration");
            if (!durationStr.empty()) {
                return ParseRelTimeToMs(durationStr);
            }
        }
        return 0;
    }
};

// ================= SonosController 公开接口 =================
SonosController::SonosController() : pImpl(std::make_unique<Impl>()) {}
SonosController::~SonosController() = default;

bool SonosController::Initialize() {
    if (pImpl->initialized) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    pImpl->initialized = true;
    return true;
}

std::vector<DeviceEntry> SonosController::SearchDevices(int totalTimeoutSec) {
    auto adapters = GetPhysicalAdapters();
    if (adapters.empty()) return {};

    struct State {
        std::mutex mtx;
        std::vector<SonosDevice> devices;
        std::unordered_set<std::string> locSet;
        int pending = 0;
    };
    auto state = std::make_shared<State>();
    state->pending = (int)adapters.size();

    std::vector<std::future<void>> tasks;
    for (const auto& adp : adapters) {
        int timeout = (adp.priorityScore >= 20) ? 5 : 2;
        tasks.push_back(std::async(std::launch::async,
            [state, adp, timeout]() {
                auto devs = DiscoverSonosDevices(adp.ip, timeout);
                std::lock_guard<std::mutex> lk(state->mtx);
                for (auto& d : devs) {
                    if (state->locSet.insert(d.location).second) {
                        state->devices.push_back(d);
                    }
                }
                state->pending--;
            }));
    }

    // 等待所有任务完成（这里简单等待，production 中可加超时判定）
    for (auto& t : tasks) t.wait();

    std::vector<DeviceEntry> entries;
    for (size_t i = 0; i < state->devices.size(); ++i) {
        entries.push_back({ (int)i, state->devices[i].ip, state->devices[i].location });
    }
    return entries;
}

bool SonosController::ConnectToDevice(const std::string& location) {
    if (!pImpl->initialized) return false;
	OutputDebugStringA(("Connecting to: " + location + "\n").c_str());
    pImpl->sonosSystem = std::make_unique<SONOS::System>(
        static_cast<void*>(0),
        static_cast<SONOS::EventCB>(nullptr)
    );
    if (!pImpl->sonosSystem->Discover(location)) {
        pImpl->sonosSystem.reset();
        return false;
    }
    pImpl->connected = true;
    return true;
}

bool SonosController::StartHttpServer(const std::wstring& musicFolder, int port) {
    return pImpl->StartHttpServer(musicFolder, port);
}

void SonosController::StopHttpServer() {
    pImpl->StopHttpServer();
}

NSROOT::PlayerPtr SonosController::GetPlayer() {
    return pImpl->GetPlayer();
}

bool SonosController::PlayFile(const std::string& filename) {
    return pImpl->PlayFile(filename);
}

std::vector<std::string> SonosController::ListMusicFiles() {
    return pImpl->ListMusicFiles();
}

std::string SonosController::GetLocalIP() const {
    return pImpl->localIP;
}

int SonosController::GetHttpPort() const {
    return pImpl->serverPort;
}
int SonosController::GetPosition() {
    return pImpl->GetPosition();
}
int SonosController::GetSonosTrackDuration() {
    return pImpl->GetSonosTrackDuration();
}
void SonosController::SetPlayPos(int pos) {
    pImpl->SetPlayPos(pos);
}
void SonosController::Stop() {
    pImpl->Stop();
}