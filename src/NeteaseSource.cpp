#include "pch.h"
#include "MusicSource.h"
#include <set>
#include <unordered_map>
#include <platform/platform.h>
#include <ManageConfig.h>
#include <NeteaseAPIManager.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <mutex>
#include <utils.h>

#include <imgui/imgui.h>
using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════
// ★ 本模块私有设置存储
// ═══════════════════════════════════════════════════════════
static std::string s_apiAddress = "";
static std::string s_playlistId = "";
static bool        s_skipVip = false;



static void LoadNeteaseSettingsFromConfig() {
    auto& cfg = ManageConfig::GetInstance();
    s_apiAddress = cfg.GetValue<std::string>("netease_api_address", "");
    s_playlistId = cfg.GetValue<std::string>("netease_playlist_id", "");
    s_skipVip = cfg.GetValue<bool>("skip_vip", false);
}

struct NeteaseTrackInfo : public BaseTrackInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtUrl;
    int fee = 0;
    bool isDetailLoaded = false;
    int retryCount = 0; // ★ 防死循环重试计数
};

class NeteaseSource : public IMusicSource {
    std::vector<std::unique_ptr<NeteaseTrackInfo>> m_tracks;
    std::mutex m_loadMtx;
    std::set<size_t> m_loadingIndices;

    // ★ 新增缓存与同步成员
    std::string m_playlistID;
    std::string m_cachePath;
    std::atomic<bool> m_hasPendingSync{ false };
    std::vector<std::string> m_pendingIds;
    std::atomic<bool> m_windowOpen{ false };
    char m_phoneInput[32] = "";
    char m_captchaInput[16] = "";

    // ★ 线程安全的 UI 状态信息
    std::mutex m_msgMtx;
    std::string m_statusMsg = "请登录网易云账号以获取完整功能";

    std::chrono::steady_clock::time_point m_lastCaptchaTime;
    std::atomic<bool> m_sendingCaptcha{ false };
    const int CAPTCHA_COOLDOWN = 60;

    void DoSendCaptcha();
    void DoLogin();

    // ★ 加锁读写状态信息
    void SetStatusMsg(const std::string& msg) {
        std::lock_guard<std::mutex> lock(m_msgMtx);
        m_statusMsg = msg;
    }
    std::string GetStatusMsg() {
        std::lock_guard<std::mutex> lock(m_msgMtx);
        return m_statusMsg;
    }

    const char* DisplayName() const override { return "网易云"; }

    // ★ 异步保存缓存（只存 ID, Title, Fee）
    void SaveCache() {
        if (m_cachePath.empty()) return;
        try {
            json j;
            j["playlist_id"] = m_playlistID;
            j["tracks"] = json::array();
            {
                std::lock_guard<std::mutex> lock(m_loadMtx);
                for (const auto& t : m_tracks) {
                    json item;
                    item["id"] = t->id;
                    item["title"] = t->title;
                    item["fee"] = t->fee;
                    j["tracks"].push_back(item);
                }
            }
            std::string path = m_cachePath;
            std::string dump = j.dump();
            std::thread([path, dump]() {
                std::ofstream f(path);
                if (f.is_open()) f << dump;
                }).detach();
        }
        catch (...) {}
    }

public:
    virtual ~NeteaseSource() noexcept = default;

    bool Init() override {
        auto& cfg = ManageConfig::GetInstance();
        auto& api = NeteaseAPIManager::GetInstance();

        // =========================================================
        // ★ 第一步：无论如何，先加载并设置账号 Cookie
        // =========================================================
        std::string base64_encryptedCookie = cfg.GetValue<std::string>("encrypted_cookie", "");
        std::string decryptedCookie = "";
        if (!base64_encryptedCookie.empty()) {
            std::string encrypted = Base64Decode(base64_encryptedCookie);
            DecryptString(encrypted, decryptedCookie);
        }

        if (!decryptedCookie.empty()) {
            api.SetUserCookie(decryptedCookie);
        }
        else {
            std::thread([]() {
                NeteaseAPIManager::GetInstance().LoginAsGuest();
                }).detach();
        }

        // 后台静默刷新 Cookie
        long long last_update = cfg.GetValue("last_update_cookie", 0LL);
        std::time_t now = std::time(nullptr);
        if (now - last_update > 24 * 60 * 60 && !decryptedCookie.empty()) {
            std::thread([]() {
                auto& api = NeteaseAPIManager::GetInstance();
                std::string current = api.GetCurrentCookie();
                if (api.UpdateCookie(current)) {
                    std::string newCookie = api.GetCurrentCookie();
                    std::string encrypted;
                    if (EncryptString(newCookie, encrypted)) {
                        ManageConfig::GetInstance().SetValue("encrypted_cookie", Base64Encode(encrypted));
                        ManageConfig::GetInstance().SetValue("last_update_cookie", (long long)std::time(nullptr));
                        ManageConfig::GetInstance().Save();
                    }
                }
                }).detach();
        }

        // =========================================================
        // 第二步：歌单加载（缓存 -> 网络）
        // =========================================================
        m_playlistID = cfg.GetValue<std::string>("netease_playlist_id", "");
        if (m_playlistID.empty()) return false;

        // 1. 确定缓存路径
        std::string tempDir = GetTempDir() + "Vacuum Music Player/NeteaseCache/";
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(Utf8ToPath(tempDir)), ec);
        m_cachePath = tempDir + "playlist_" + m_playlistID + ".json";

        // 2. 尝试从本地缓存极速启动
        if (std::filesystem::exists(std::filesystem::path(Utf8ToPath(m_cachePath)), ec)) {
            try {
                std::ifstream f(std::filesystem::path(Utf8ToPath(m_cachePath)));
                json j;
                f >> j;
                if (j.contains("tracks") && j["tracks"].is_array()) {
                    for (auto& item : j["tracks"]) {
                        auto track = std::make_unique<NeteaseTrackInfo>();
                        track->id = item.value("id", "");
                        track->title = item.value("title", "加载中...");
                        track->fee = item.value("fee", 0);

                        std::string prefix = (track->fee == 1 || track->fee == 2) ? "[VIP] " : "";
                        track->displayName = prefix + track->title;
                        track->isDetailLoaded = false;

                        m_tracks.push_back(std::move(track));
                    }
                }
                if (!m_tracks.empty()) {
                    OutputDebugStringA("[NeteaseSource] 从本地 JSON 缓存极速启动！\n");
                    return true;
                }
            }
            catch (...) {
                m_tracks.clear();
            }
        }

        // 3. 没缓存，走网络同步拉取 ID
        try {
            auto ids = NeteaseAPIManager::GetInstance().GetPlaylistTrackIds(std::stoll(m_playlistID));
            for (auto& id : ids) {
                auto track = std::make_unique<NeteaseTrackInfo>();
                track->id = id;
                track->title = "加载中...";
                track->displayName = "ID: " + id + " (滚动以加载...)";
                track->isDetailLoaded = false;
                m_tracks.push_back(std::move(track));
            }
            SaveCache();
            return !m_tracks.empty();
        }
        catch (...) {
            return false;
        }
    }

    void SyncPlaylistAsync() override {
        if (m_playlistID.empty()) return;
        std::string pid = m_playlistID;
        std::thread([this, pid]() {
            try {
                auto newIds = NeteaseAPIManager::GetInstance().GetPlaylistTrackIds(std::stoll(pid));
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(m_loadMtx);
                    if (newIds.size() != m_tracks.size()) changed = true;
                    else {
                        for (size_t i = 0; i < newIds.size(); ++i) {
                            if (newIds[i] != m_tracks[i]->id) { changed = true; break; }
                        }
                    }
                }

                if (changed) {
                    std::lock_guard<std::mutex> lock(m_loadMtx);
                    m_pendingIds = newIds;
                    m_hasPendingSync = true;
                }
            }
            catch (...) {
                OutputDebugStringA("[NeteaseSource] 后台同步歌单失败\n");
            }
            }).detach();
    }

    void RequestTrackDetails(size_t startIndex, size_t endIndex) override {
        if (m_hasPendingSync.exchange(false)) {
            std::lock_guard<std::mutex> lock(m_loadMtx);
            std::unordered_map<std::string, std::unique_ptr<NeteaseTrackInfo>> oldMap;
            for (auto& t : m_tracks) oldMap[t->id] = std::move(t);

            m_tracks.clear();
            for (const auto& id : m_pendingIds) {
                if (oldMap.find(id) != oldMap.end()) {
                    m_tracks.push_back(std::move(oldMap[id]));
                    oldMap.erase(id);
                }
                else {
                    auto track = std::make_unique<NeteaseTrackInfo>();
                    track->id = id;
                    track->title = "加载中...";
                    track->displayName = "⏳ [正在获取详情...] (ID: " + id + ")";
                    m_tracks.push_back(std::move(track));
                }
            }
            m_pendingIds.clear();
            SaveCache();
            PostMessage(g_hWnd, WM_APP + 6, 0, 0); // WM_PLAYLIST_UPDATED
        }

        std::vector<std::string> idsToLoad;
        std::vector<size_t> indicesToLoad;
        {
            std::lock_guard<std::mutex> lock(m_loadMtx);
            for (size_t i = startIndex; i < endIndex && i < m_tracks.size(); ++i) {
                if (!m_tracks[i]->isDetailLoaded && m_tracks[i]->retryCount < 3 &&
                    m_loadingIndices.find(i) == m_loadingIndices.end()) {
                    idsToLoad.push_back(m_tracks[i]->id);
                    indicesToLoad.push_back(i);
                    m_loadingIndices.insert(i);
                }
            }
        }

        if (idsToLoad.empty()) return;

        std::thread([this, idsToLoad, indicesToLoad]() {
            bool needUpdateCache = false;
            try {
                auto details = NeteaseAPIManager::GetInstance().GetSongsDetail(idsToLoad);
                std::lock_guard<std::mutex> lock(m_loadMtx);
                for (size_t i = 0; i < details.size() && i < indicesToLoad.size(); ++i) {
                    size_t idx = indicesToLoad[i];
                    const auto& s = details[i];

                    m_tracks[idx]->title = s.name;
                    m_tracks[idx]->album = s.album;
                    m_tracks[idx]->albumArtUrl = s.albumArtUrl;
                    m_tracks[idx]->fee = s.fee;

                    std::string artistsStr;
                    for (size_t j = 0; j < s.artists.size(); ++j) {
                        if (j > 0) artistsStr += ", ";
                        artistsStr += s.artists[j];
                    }
                    m_tracks[idx]->artist = artistsStr;

                    std::string prefix = (s.fee == 1 || s.fee == 2) ? "[VIP] " : "";
                    m_tracks[idx]->displayName = prefix + s.name + " - " + artistsStr;

                    m_tracks[idx]->isDetailLoaded = true;
                    m_loadingIndices.erase(idx);
                    needUpdateCache = true;
                }
            }
            catch (...) {
                std::lock_guard<std::mutex> lock(m_loadMtx);
                for (size_t idx : indicesToLoad) {
                    m_loadingIndices.erase(idx);
                    m_tracks[idx]->retryCount++;
                }
            }
            if (needUpdateCache) SaveCache();
            }).detach();
    }

    bool HasWindow() const override { return true; }
    void ShowWindow() override { m_windowOpen.store(true); }

    void RenderWindow() override {
        if (!m_windowOpen.load()) return;

        bool open = m_windowOpen.load();
        ImGui::SetNextWindowSize(ImVec2(380, 230), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("网易云账号登录", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
            ImGui::End();
            m_windowOpen.store(open);
            return;
        }

        auto& api = NeteaseAPIManager::GetInstance();
        std::string currentCookie = api.GetCurrentCookie();
        std::string status = GetStatusMsg();

        if (!currentCookie.empty() && status != "登录中..." && status != "登录成功！") {
            std::string username = api.GetNeteaseUserName();
            if (username.empty()) username = "已登录 (无法获取昵称)";
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "状态: %s", username.c_str());
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("退出登录", ImVec2(-1, 30))) {
                api.SetUserCookie("");
                ManageConfig::GetInstance().SetValue("encrypted_cookie", "");
                ManageConfig::GetInstance().Save();
                api.LoginAsGuest();
                SetStatusMsg("已退出登录");
            }
            ImGui::End();
            m_windowOpen.store(open);
            return;
        }

        ImGui::Text("手机号");
        ImGui::SameLine(60);
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##PhoneInput", m_phoneInput, sizeof(m_phoneInput));

        ImGui::SameLine();
        auto now = std::chrono::steady_clock::now();
        int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastCaptchaTime).count();
        bool canSend = (elapsed >= CAPTCHA_COOLDOWN) && !m_sendingCaptcha.load();

        if (canSend) {
            if (ImGui::Button("获取验证码")) DoSendCaptcha();
        }
        else {
            std::string btnLabel = m_sendingCaptcha ? "发送中..." : "等待 " + std::to_string(CAPTCHA_COOLDOWN - elapsed) + "s";
            ImGui::BeginDisabled();
            ImGui::Button(btnLabel.c_str());
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Text("验证码");
        ImGui::SameLine(60);
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##CaptchaInput", m_captchaInput, sizeof(m_captchaInput));

        ImGui::Spacing();
        ImGui::Spacing();
        if (ImGui::Button("登录", ImVec2(-1, 30))) DoLogin();

        ImGui::Spacing();
        ImGui::TextDisabled("%s", status.c_str());

        ImGui::End();
        m_windowOpen.store(open);
    }

    int FindTrackIndex(const std::string& id) const override {
        std::lock_guard<std::mutex> lock(const_cast<NeteaseSource*>(this)->m_loadMtx);
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i]->id == id) return (int)i;
        }
        return -1;
    }

    int AddTrack(const std::string& id) override {
        std::lock_guard<std::mutex> lock(m_loadMtx);
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i]->id == id) return (int)i;
        }

        auto track = std::make_unique<NeteaseTrackInfo>();
        track->id = id;
        track->title = "加载中...";
        track->displayName = "⏳ [正在获取详情...] (ID: " + id + ")";
        m_tracks.push_back(std::move(track));

        int newIdx = (int)m_tracks.size() - 1;
        std::thread([this]() { SaveCache(); }).detach();
        return newIdx;
    }

    size_t GetCount() const override { return m_tracks.size(); }
    const BaseTrackInfo* GetBaseTrack(size_t index) const override { return m_tracks[index].get(); }

    PlaybackInfo PreparePlayback(size_t index) override {
        PlaybackInfo info;
        if (index >= m_tracks.size()) return info;
        const auto& t = m_tracks[index];

        if (!t->isDetailLoaded) {
            try {
                auto details = NeteaseAPIManager::GetInstance().GetSongsDetail({ t->id });
                if (!details.empty()) {
                    const auto& s = details[0];
                    t->title = s.name;
                    t->album = s.album;
                    t->albumArtUrl = s.albumArtUrl;
                    t->fee = s.fee;
                    std::string artistsStr;
                    for (size_t j = 0; j < s.artists.size(); ++j) {
                        if (j > 0) artistsStr += ", ";
                        artistsStr += s.artists[j];
                    }
                    t->artist = artistsStr;
                    t->displayName = ((s.fee == 1 || s.fee == 2) ? "[VIP] " : "") + s.name + " - " + artistsStr;
                    t->isDetailLoaded = true;
                }
            }
            catch (...) {}
        }

        if (s_skipVip && (t->fee == 1 || t->fee == 2)) {
            info.isValid = false;
            OutputDebugStringA("[NeteaseSource] 拦截并跳过 VIP 歌曲\n");
            return info;
        }

        info.isOnline = true;
        info.localCacheId = t->id;
        info.title = t->title;
        info.artist = t->artist;
        info.album = t->album;
        info.needTags = true;
        info.lyrics = NeteaseAPIManager::GetInstance().GetLyrics(t->id);
        info.albumArtUrl = t->albumArtUrl;
        info.audioUrl = NeteaseAPIManager::GetInstance().GetSongUrl(t->id, "exhigh");
        info.isValid = !info.audioUrl.empty();
        return info;
    }

    void AddSettings(std::vector<std::unique_ptr<SettingItemBase>>& items) override {
        LoadNeteaseSettingsFromConfig();

        items.push_back(std::make_unique<SettingItemText>(
            s_apiAddress, "netease_api_address",
            "网易云API地址（BaseURL）\n例子：http://localhost:3000/\n注意：末尾要有斜杠“/”"));
        items.push_back(std::make_unique<SettingItemText>(
            s_playlistId, "netease_playlist_id",
            "网易云歌单ID\n例子：https://music.163.com/playlist?id=xxxxxxx\n其中的 xxxxxxx 部分填入"));
        items.push_back(std::make_unique<SettingItem<bool>>(
            s_skipVip, "skip_vip", "跳过需要 VIP 的音乐"));
        
    }

    int GetLastIndex() const override {
        auto& cfg = ManageConfig::GetInstance();
        std::string last = cfg.GetValue<std::string>("last_music_online", "");
        if (last.empty()) return 0;
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i]->id == last) return (int)i;
        return 0;
    }

    void SaveIndex(int index) override {
        if (index < 0 || index >= (int)m_tracks.size()) return;
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue("last_music_online", m_tracks[index]->id);
        cfg.SetValue("last_music_local", "");
    }
};

void NeteaseSource::DoSendCaptcha() {
    std::string phone = m_phoneInput;
    if (phone.size() != 11) { SetStatusMsg("请输入有效的11位手机号"); return; }
    m_lastCaptchaTime = std::chrono::steady_clock::now();
    m_sendingCaptcha = true;
    SetStatusMsg("正在发送验证码...");

    // ★ 改用 std::thread 并加上 try-catch，彻底杜绝崩溃
    std::thread([this, phone]() {
        try {
            bool ok = NeteaseAPIManager::GetInstance().SendCaptcha(phone);
            m_sendingCaptcha = false;
            SetStatusMsg(ok ? "验证码已发送" : "发送失败，请重试");
        }
        catch (const std::exception& e) {
            m_sendingCaptcha = false;
            SetStatusMsg(std::string("异常: ") + e.what());
        }
        catch (...) {
            m_sendingCaptcha = false;
            SetStatusMsg("未知异常");
        }
        }).detach();
}

void NeteaseSource::DoLogin() {
    std::string phone = m_phoneInput;
    std::string captcha = m_captchaInput;
    if (phone.empty() || captcha.empty()) { SetStatusMsg("请填写完整信息"); return; }
    SetStatusMsg("正在登录...");

    // ★ 改用 std::thread 并加上 try-catch，彻底杜绝崩溃
    std::thread([this, phone, captcha]() {
        try {
            auto& api = NeteaseAPIManager::GetInstance();
            bool ok = api.LoginByCaptcha(phone, captcha);
            if (ok) {
                std::string cookie = api.GetCurrentCookie();
                if (!cookie.empty()) {
                    std::string encrypted;
                    if (EncryptString(cookie, encrypted)) {
                        auto& cfg = ManageConfig::GetInstance();
                        cfg.SetValue("encrypted_cookie", Base64Encode(encrypted));
                        cfg.SetValue("last_update_cookie", (long long)std::time(nullptr));
                        cfg.Save();
                    }
                }
                SetStatusMsg("登录成功！");
                m_windowOpen = false; // 登录成功自动关窗
            }
            else {
                SetStatusMsg("登录失败，请检查验证码或网络");
            }
        }
        catch (const std::exception& e) {
            SetStatusMsg(std::string("异常: ") + e.what());
        }
        catch (...) {
            SetStatusMsg("未知异常");
        }
        }).detach();
}

std::unique_ptr<IMusicSource> CreateNeteaseSource() { return std::make_unique<NeteaseSource>(); }