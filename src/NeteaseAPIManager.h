#pragma once
#define CPPHTTPLIB_USE_WIN32_FILE_IO 0
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <platform/platform.h>

using json = nlohmann::json;

struct NeteaseSongInfo {
    std::string id;
    std::string name;
    std::vector<std::string> artists;
    std::string album;
    int duration = 0;      // 毫秒
    int fee = 0;           // 0免费,1VIP,4购买专辑,8非会员低音质完整
    std::string playUrl;   // 播放链接（可能为空）
    std::string trialUrl;  // 试听链接（30秒）
    std::string albumArtUrl;
    std::string fileType; //音频扩展名
};

enum class ApiSource { Official, ThirdParty };

struct CircuitBreaker {
    int  consecutiveFails = 0;
    int  failThreshold = 3;         // 连续失败 3 次触发
    int  cooldownSeconds = 60;        // 冷却 60 秒
    std::chrono::steady_clock::time_point cooldownUntil;
    std::mutex mtx;

    bool IsInCooldown() {
        std::lock_guard<std::mutex> lock(mtx);
        return std::chrono::steady_clock::now() < cooldownUntil;
    }
    void OnSuccess() {
        std::lock_guard<std::mutex> lock(mtx);
        consecutiveFails = 0;
    }
    void OnFailure() {
        std::lock_guard<std::mutex> lock(mtx);
        consecutiveFails++;
        if (consecutiveFails >= failThreshold) {
            cooldownUntil = std::chrono::steady_clock::now()
                + std::chrono::seconds(cooldownSeconds);
            consecutiveFails = 0;   // 冷却后重新计数
        }
    }
    void Reset() {
        std::lock_guard<std::mutex> lock(mtx);
        consecutiveFails = 0;
        cooldownUntil = std::chrono::steady_clock::time_point{};
    }
};

struct RateLimiter {
    int  limitPerMinute = 60;
    int  count = 0;
    std::chrono::steady_clock::time_point windowStart;
    std::mutex mtx;

    bool TryAcquire() {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        if (windowStart.time_since_epoch().count() == 0
            || now - windowStart > std::chrono::minutes(1)) {
            windowStart = now;
            count = 0;
        }
        if (count >= limitPerMinute) return false;
        count++;
        return true;
    }
};

class NeteaseAPIManager {
public:
    static NeteaseAPIManager& GetInstance();

    // 设置 API 基础地址
    void SetBaseURL(const std::string& url);

    // 游客登录，获取 Cookie（自动调用，也可手动触发）
    bool LoginAsGuest();

    // 获取歌单所有歌曲 ID（返回 trackIds）
    std::vector<std::string> GetPlaylistTrackIds(int64_t playlistId);

    // 批量获取歌曲详情（包含 fee 等信息）
    std::vector<NeteaseSongInfo> GetSongsDetail(const std::vector<std::string>& ids);

    // 获取单首歌曲播放 URL（可指定音质：standard, higher, exhigh, lossless, hires）
    std::string GetSongUrl(const std::string& songId, const std::string& level = "exhigh",const std::string& cookie="");

    // 获取歌曲完整信息（包括 URL 和 fee）
    NeteaseSongInfo GetSongFullInfo(const std::string& songId, const std::string& level = "exhigh");

    // 搜索歌曲（返回歌曲列表，含基本信息）
    std::vector<NeteaseSongInfo> SearchSongs(const std::string& keyword, int limit = 30);

    // 设置是否启用游客登录（默认启用）
    void SetEnableGuestLogin(bool enable) { m_enableGuest = enable; }

    std::string GetLyrics(const std::string& songId);

    bool UpdateCookie(const std::string& cookie);
    void SetUserCookie(const std::string& cookie);   // 设置用户 Cookie（覆盖游客模式）
    void ClearUserCookie();                          // 清除用户 Cookie，回退到游客（如果有）
    bool HasUserCookie() const { return !m_userCookie.empty(); }
    std::string GetCurrentCookie() const;
    bool SendCaptcha(const std::string& phone);

    // 验证码登录
    bool LoginByCaptcha(const std::string& phone, const std::string& captcha);

    // 获取当前登录状态
    bool IsLoggedIn() const { return !m_userCookie.empty(); }

    std::string GetNeteaseUserName();

private:
    NeteaseAPIManager() = default;
    ~NeteaseAPIManager() = default;

    // 通用请求函数（支持额外 headers）
    json Request(const std::string& officialPath,
        const std::string& thirdPartyPath,
        const std::vector<std::pair<std::string, std::string>>& params,
        ApiSource primary = ApiSource::Official,   // 主源
        bool needUserCookie = false);              // 仅 ThirdParty 时生效
    json DoRequest(ApiSource source,
        const std::string& path,
        const std::vector<std::pair<std::string, std::string>>& params,
        bool needUserCookie);

    json RequestThidParty(const std::string& path,
        const std::vector<std::pair<std::string, std::string>>& params,
        const httplib::Headers& extraHeaders = {});

    std::string m_userCookie;
    std::string m_baseUrl;
    std::string m_guestCookie;
    bool m_enableGuest = true;
    bool m_guestLoggedIn = false;

    // 内部辅助
    std::string SafeGetString(const nlohmann::json& obj, const std::string& key, const std::string& defaultVal = "");
    std::vector<std::string> ExtractTrackIds(const nlohmann::json& trackIdsJson);
    mutable std::mutex m_mutex;   // 保护内部 Cookie 状态

    CircuitBreaker m_officialCircuit;
    CircuitBreaker m_thirdPartyCircuit;
    RateLimiter    m_officialLimiter;
    RateLimiter    m_thirdPartyLimiter;

    // ========== 新增：统一请求入口 ==========
    // 两个路径分别对应官方和第三方；auto 决定主备顺序
    json RequestWithRoute(
        ApiSource primary,
        const std::string& officialPath,
        const std::string& thirdPartyPath,
        const std::vector<std::pair<std::string, std::string>>& params,
        bool needUserCookie = false);   // 是否需要用户 Cookie
    

};

//bool EncryptStringWithDPAPI(const std::string& plaintext, std::string& ciphertext);
//bool DecryptStringWithDPAPI(const std::string& ciphertext, std::string& plaintext);
//std::string Base64Encode(const std::string& binary);
//std::string Base64Decode(const std::string& base64);
//再见 已经移入平台和main.cpp有关处了 