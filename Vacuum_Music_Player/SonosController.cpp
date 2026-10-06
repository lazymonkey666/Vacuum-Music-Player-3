#include "SonosController.h"

#include <platform/platform.h>
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


extern WindowHandle g_hWnd;


#ifndef WM_SONOS_STOPPED
#define WM_SONOS_STOPPED (WM_APP + 300)
#endif
// ================= 工具函数 =================
namespace {

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

        auto adapters = EnumNetworkAdapters();   // ← 平台函数

        for (const auto& adp : adapters) {
            std::string ip = adp.ip;
            if (ip == "127.0.0.1") continue;      // loopback

            std::string lowerDesc = ToLower(adp.description);

            // 过滤虚拟网卡（业务逻辑保留）
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
            info.description = adp.description;
            info.hasGateway = adp.hasGateway;

            // 打分（业务逻辑保留）
            info.priorityScore = 0;
            if (adp.type == 1) info.priorityScore += 10;   // ethernet
            else if (adp.type == 2) info.priorityScore += 5;    // wifi

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

            result.push_back(std::move(info));
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
        std::string friendlyName;
    };

    std::vector<SonosDevice> DiscoverSonosDevices(const std::string& localIP, int timeoutSec) {
        std::vector<SonosDevice> devices;
    
        OutputDebugStringA(("\n[ssdp] ========== DiscoverSonosDevices start ==========\n"));
        OutputDebugStringA(("[ssdp] localIP=" + localIP +
                            " timeout=" + std::to_string(timeoutSec) + "s\n").c_str());
    
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            OutputDebugStringA("[ssdp] socket() failed\n");
            return devices;
        }
        OutputDebugStringA(("[ssdp] socket created, fd=" + std::to_string(sock) + "\n").c_str());
    
        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        int ttl = 4;
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    
        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(0);
        if (inet_pton(AF_INET, localIP.c_str(), &local.sin_addr) != 1) {
            OutputDebugStringA(("[ssdp] inet_pton failed for localIP: " + localIP + "\n").c_str());
            closesocket(sock);
            return devices;
        }
    
        if (bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
            OutputDebugStringA(("[ssdp] bind() failed to " + localIP + ":0, errno=" +
                                std::to_string(errno) + "\n").c_str());
            closesocket(sock);
            return devices;
        }
        OutputDebugStringA(("[ssdp] bound to " + localIP + ":0\n").c_str());
    
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
    
        int sent = sendto(sock, query, (int)strlen(query), 0, (sockaddr*)&dest, sizeof(dest));
        if (sent == SOCKET_ERROR) {
            OutputDebugStringA(("[ssdp] sendto failed, errno=" +
                                std::to_string(errno) + "\n").c_str());
            closesocket(sock);
            return devices;
        }
        OutputDebugStringA(("[ssdp] M-SEARCH sent (" + std::to_string(sent) + " bytes) to 239.255.255.250:1900\n").c_str());
    
        fd_set readfds;
        timeval tv = { timeoutSec, 0 };
        char buf[4096];
        int responseCount = 0;
        int iteration = 0;
    
        while (true) {
            iteration++;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
    
            // ★ 关键修复：Linux 上 select 需要 max_fd + 1
            int sel = select(sock + 1, &readfds, nullptr, nullptr, &tv);
            if (sel <= 0) {
                OutputDebugStringA(("[ssdp] select timeout after " +
                                    std::to_string(iteration) + " iterations (got " +
                                    std::to_string(responseCount) + " responses)\n").c_str());
                break;
            }
            OutputDebugStringA(("[ssdp] select ready, iteration=" +
                                std::to_string(iteration) + "\n").c_str());
    
            sockaddr_in from;
            socklen_t fromLen = sizeof(from);
            int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
            if (n <= 0) {
                OutputDebugStringA(("[ssdp] recvfrom failed, errno=" +
                                    std::to_string(errno) + "\n").c_str());
                break;
            }
            buf[n] = '\0';
            std::string response(buf);
    
            char fromIpStr[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &from.sin_addr, fromIpStr, sizeof(fromIpStr));
            responseCount++;
            OutputDebugStringA(("[ssdp] response #" + std::to_string(responseCount) +
                                " from " + fromIpStr + " (" +
                                std::to_string(n) + " bytes)\n").c_str());
    
            // 打印响应前 200 字节（直观判断内容格式）
            std::string preview = response.substr(0, std::min<size_t>(200, response.size()));
            for (auto& c : preview) if (c == '\r' || c == '\n') c = ' ';
            OutputDebugStringA(("[ssdp]   preview: " + preview + "\n").c_str());
    
            auto findHeader = [&](const std::string& header) -> std::string {
                size_t pos = response.find(header + ": ");
                if (pos == std::string::npos) return "";
                pos += header.size() + 2;
                size_t end = response.find("\r\n", pos);
                return response.substr(pos, end - pos);
            };
    
            std::string location = findHeader("LOCATION");
            OutputDebugStringA(("[ssdp]   LOCATION = " + (location.empty() ? "(empty)" : location) + "\n").c_str());
    
            if (!location.empty()) {
                bool exists = false;
                for (auto& d : devices) if (d.location == location) { exists = true; break; }
                if (exists) {
                    OutputDebugStringA("[ssdp]   duplicate, skipped\n");
                    continue;
                }
    
                SonosDevice dev;
                dev.location = location;
                dev.server = findHeader("SERVER");
                dev.ip = fromIpStr;
    
                std::string host, path;
                size_t proto_pos = location.find("://");
                if (proto_pos != std::string::npos) {
                    size_t host_start = proto_pos + 3;
                    size_t host_end = location.find('/', host_start);
                    if (host_end == std::string::npos) host_end = location.size();
                    host = location.substr(host_start, host_end - host_start);
                    path = (host_end < location.size()) ? location.substr(host_end) : "/";
                }
    
                std::string hostname = host;
                int port = 80;
                size_t colon = host.find(':');
                if (colon != std::string::npos) {
                    hostname = host.substr(0, colon);
                    port = std::stoi(host.substr(colon + 1));
                }
    
                OutputDebugStringA(("[ssdp]   fetching device description: http://" +
                                    hostname + ":" + std::to_string(port) + path + "\n").c_str());
    
                httplib::Client cli(hostname, port);
                cli.set_connection_timeout(2);
                cli.set_read_timeout(2);
                auto res = cli.Get(path.c_str());
    
                if (res && res->status == 200) {
                    OutputDebugStringA(("[ssdp]   GET OK, body size=" +
                                        std::to_string(res->body.size()) + "\n").c_str());
    
                    const std::string& body = res->body;
                    std::string roomName;
                    const std::string mediaRendererType =
                        "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>";
                    size_t typePos = body.find(mediaRendererType);
                    if (typePos != std::string::npos) {
                        size_t deviceStart = body.rfind("<device", typePos);
                        if (deviceStart != std::string::npos) {
                            size_t friendlyStart = body.find("<friendlyName>", deviceStart);
                            if (friendlyStart != std::string::npos) {
                                friendlyStart += 13;
                                size_t friendlyEnd = body.find("</friendlyName>", friendlyStart);
                                if (friendlyEnd != std::string::npos) {
                                    std::string fullName = body.substr(friendlyStart + 1, friendlyEnd - friendlyStart);
                                    size_t dash = fullName.find(" - ");
                                    roomName = (dash != std::string::npos) ? fullName.substr(0, dash) : fullName;
                                }
                            }
                        }
                    }
                    if (roomName.empty()) {
                        const std::string tagOpen = "<friendlyName>";
                        const std::string tagClose = "</friendlyName>";
                        size_t start = body.find(tagOpen);
                        if (start != std::string::npos) {
                            start += tagOpen.size();
                            size_t end = body.find(tagClose, start);
                            if (end != std::string::npos) {
                                std::string rootName = body.substr(start, end - start);
                                size_t dash = rootName.find(" - ");
                                roomName = (dash != std::string::npos) ? rootName.substr(0, dash) : rootName;
                            }
                        }
                    }
    
                    if (!roomName.empty()) {
                        dev.friendlyName = roomName + " - " + dev.ip;
                        OutputDebugStringA(("[ssdp]   device added: " + dev.friendlyName + "\n").c_str());
                    } else {
                        dev.friendlyName = dev.ip;
                        OutputDebugStringA(("[ssdp]   device added (no room name): " + dev.ip + "\n").c_str());
                    }
                } else {
                    OutputDebugStringA(("[ssdp]   GET failed, status=" +
                                        std::to_string(res ? res->status : -1) + "\n").c_str());
                    dev.friendlyName = dev.ip;
                }
                devices.push_back(dev);
            } else {
                OutputDebugStringA("[ssdp]   no LOCATION header, skipped\n");
            }
        }
    
        closesocket(sock);
    
        OutputDebugStringA(("[ssdp] ========== DiscoverSonosDevices end, " +
                            std::to_string(devices.size()) + " device(s) ==========\n\n").c_str());
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

    std::chrono::steady_clock::time_point m_playStartTime;
    int m_cachedPositionMs = 0;          // 最后一次校准的进度（毫秒）
    bool m_isPlaying = false;
    std::mutex m_timeMutex;
    std::atomic<bool> m_stopPolling{ false };
    std::thread m_pollThread;
    std::string m_transportState;

    // 方法声明
    void StartPolling();
    void StopPolling();
    void PollLoop();
    int GetPosition();  // 供 UI 调用

    ~Impl() {
        Stop();
    }

    void Stop() {
        StopHttpServer();
        StopPolling();
        sonosSystem.reset();
        connected = false;
        if (initialized) {
            CleanupNetwork();
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
        // 1. 先直接试路由法：连一个外部地址，看内核选哪张网卡出去
        {
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s != INVALID_SOCKET) {
                sockaddr_in dst = {};
                dst.sin_family = AF_INET;
                dst.sin_port = htons(53);
                inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);
                if (connect(s, (sockaddr*)&dst, sizeof(dst)) == 0) {
                    sockaddr_in local;
                    socklen_t len = sizeof(local);
                    if (getsockname(s, (sockaddr*)&local, &len) == 0) {
                        char ipStr[INET_ADDRSTRLEN] = {0};
                        inet_ntop(AF_INET, &local.sin_addr, ipStr, sizeof(ipStr));
                        std::string ip = ipStr;
                        closesocket(s);
                        // 非空、非 127.x 就是有效出口 IP
                        if (!ip.empty() && ip.rfind("127.", 0) != 0) {
                            OutputDebugStringA(("[net] localIP (route) = " + ip + "\n").c_str());
                            return ip;
                        }
                    }
                }
                closesocket(s);
            }
        }
    
        // 2. fallback：扫网卡，取第一个非环回 IPv4
        for (const auto& adp : EnumNetworkAdapters()) {
            if (!adp.ip.empty() && adp.ip.rfind("127.", 0) != 0) {
                OutputDebugStringA(("[net] localIP (adapter) = " + adp.ip + "\n").c_str());
                return adp.ip;
            }
        }
    
        // 3. 实在没有
        OutputDebugStringA("[net] localIP fallback to 127.0.0.1\n");
        return "127.0.0.1";
    }
    bool StartHttpServer(const PathType& musicFolder, int port) {
        // 1. 检查文件夹
        if (!std::filesystem::exists(musicFolder)) {
            OutputDebugStringA("[StartHttpServer] FAIL: folder does not exist");
            return false;
        }

        // 2. 获取本地IP
        localIP = GetLocalIP();
        if (localIP.empty()) localIP = "127.0.0.1";

        // 3. 端口有效性
        serverPort = port;
        if (serverPort <= 0 || serverPort > 65535) {
            OutputDebugStringA("[StartHttpServer] FAIL: invalid port");
            return false;
        }

        // 4. 路径转为 UTF-8（用平台函数）
        musicPath = PathToUtf8(musicFolder);   // ← 一行搞定
        if (musicPath.empty()) {
            OutputDebugStringA("[StartHttpServer] FAIL: PathToUtf8 error");
            return false;
        }

        // 5. 启动 HTTP 线程（参数传值拷贝，避免悬垂）
        std::string utf8Path = musicPath;
        httpRunning = true;
        httpThread = std::thread([this, utf8Path, port]() {
            httplib::Server svr;
        
            // ★ 唯一新增
            svr.set_exception_handler([](const httplib::Request& req,
                                         httplib::Response& res,
                                         std::exception_ptr ep) {
                std::string msg;
                try { std::rethrow_exception(ep); }
                catch (const std::exception& e) { msg = e.what(); }
                catch (...) { msg = "unknown"; }
                OutputDebugStringA(("[httplib] exception on " + req.method + " " +
                                    req.path + ": " + msg + "\n").c_str());
                res.status = 500;
                res.set_content("", "text/plain");
            });
        
            // ★ 可选，能看 4xx/5xx
            svr.set_error_handler([](const httplib::Request& req,
                                     httplib::Response& res) {
                OutputDebugStringA(("[httplib] error " + std::to_string(res.status) +
                                    " on " + req.method + " " + req.path + "\n").c_str());
            });
        
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
                    auto u8name = entry.path().filename().u8string();
                    std::string utf8name(reinterpret_cast<const char*>(u8name.data()), u8name.size());
                    files.push_back(utf8name);
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
        // encode but preserve '/' so subpaths remain as directories in URL
        std::string encoded;
        for (char c : filename) {
            if (c == '/') { encoded.push_back('/'); }
            else encoded += URLEncode(std::string(1, c));
        }
        std::string url = "http://" + localIP + ":" + std::to_string(serverPort) + "/" + encoded;
        DigitalItemPtr item(new DigitalItem(DigitalItem::Type_item, DigitalItem::SubType_audioItem));
        item->SetProperty("dc:title", filename);
        item->SetProperty("res", url);
        item->SetProperty("upnp:class", "object.item.audioItem.musicTrack");
        OutputDebugStringA((url + "\n").c_str());
        if (player->SetCurrentURI(item))
        {
            if (player->Play()) {
                std::lock_guard<std::mutex> lock(m_timeMutex);
                m_playStartTime = std::chrono::steady_clock::now();
                m_cachedPositionMs = 0;
                m_isPlaying = true;
                return true;
            }
            return false;
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
            // 校准：请求实际进度并重置本地计时器
            SONOS::ElementList vars;
            if (player->GetPositionInfo(vars)) {
                std::string relTime = vars.GetValue("RelTime");
                int realPos = ParseRelTimeToMs(relTime);
                std::lock_guard<std::mutex> lock(m_timeMutex);
                m_cachedPositionMs = realPos;
                m_playStartTime = std::chrono::steady_clock::now();
                m_isPlaying = true;  // 假设拖动后自动播放
            }
        }
    }
    int GetSonosTrackDuration() {
        auto player = GetPlayer();
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
    if (!InitNetwork()) return false;
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
        entries.push_back({
            (int)i,
            state->devices[i].ip,
            state->devices[i].location,
            state->devices[i].friendlyName   // 填入设备名称
            });
    }
    return entries;
}

bool SonosController::ConnectToDevice(const std::string& location) {
    if (!pImpl->initialized) return false;
    pImpl->StopPolling();
    pImpl->sonosSystem.reset();
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
    pImpl->StartPolling();
    return true;
}

bool SonosController::StartHttpServer(const PathType& musicFolder, int port) {
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

int SonosController::Impl::GetPosition() {
    std::lock_guard<std::mutex> lock(m_timeMutex);
    if (!m_isPlaying) return m_cachedPositionMs;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_playStartTime).count();
    return m_cachedPositionMs + static_cast<int>(elapsed);
}

void SonosController::Impl::StartPolling() {
    m_stopPolling = false;
    m_pollThread = std::thread(&Impl::PollLoop, this);
}

void SonosController::Impl::StopPolling() {
    m_stopPolling = true;
    if (m_pollThread.joinable()) m_pollThread.join();
}

void SonosController::Impl::PollLoop() {
    int calibrateCounter = 0;
    while (!m_stopPolling) {
        auto player = GetPlayer();
        if (player) {
            SONOS::ElementList vars;
            if (player->GetTransportInfo(vars)) {
                std::string state = vars.GetValue("CurrentTransportState");
                // 转为小写便于比较
                std::transform(state.begin(), state.end(), state.begin(), ::tolower);
                std::lock_guard<std::mutex> lock(m_timeMutex);
                if (m_transportState != state) {
                    m_transportState = state;
                    bool playing = (state == "playing");
                    m_isPlaying = playing;
                    if (playing) {
                        m_playStartTime = std::chrono::steady_clock::now();
                        m_cachedPositionMs = 0;
                    }
                    else if (state == "stopped") {
                        // 通知主线程切歌（仅当之前不是 stopped，避免重复发送）
                        ::PostMessage(g_hWnd, WM_SONOS_STOPPED, 0, 0);
                    }
                }
            }

            // 每 10 秒校准一次进度（放在状态更新之后，不影响主循环）
            calibrateCounter++;
            if (calibrateCounter >= 5) {
                calibrateCounter = 0;
                SONOS::ElementList posVars;
                if (player->GetPositionInfo(posVars)) {
                    std::string relTime = posVars.GetValue("RelTime");
                    int realPos = ParseRelTimeToMs(relTime);
                    std::lock_guard<std::mutex> lock(m_timeMutex);
                    m_cachedPositionMs = realPos;
                    m_playStartTime = std::chrono::steady_clock::now();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}