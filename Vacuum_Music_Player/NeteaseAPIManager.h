#pragma once
#define CPPHTTPLIB_USE_WIN32_FILE_IO 0
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <httplib.h>
#include <nlohmann/json.hpp>

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
    std::string GetSongUrl(const std::string& songId, const std::string& level = "exhigh");

    // 获取歌曲完整信息（包括 URL 和 fee）
    NeteaseSongInfo GetSongFullInfo(const std::string& songId, const std::string& level = "exhigh");

    // 搜索歌曲（返回歌曲列表，含基本信息）
    std::vector<NeteaseSongInfo> SearchSongs(const std::string& keyword, int limit = 30);

    // 设置是否启用游客登录（默认启用）
    void SetEnableGuestLogin(bool enable) { m_enableGuest = enable; }

    std::string GetLyrics(const std::string& songId);

private:
    NeteaseAPIManager() = default;
    ~NeteaseAPIManager() = default;

    // 通用请求函数（支持额外 headers）
    nlohmann::json Request(const std::string& path,
        const std::vector<std::pair<std::string, std::string>>& params = {},
        const httplib::Headers& extraHeaders = {});

    std::string m_baseUrl;
    std::string m_guestCookie;
    bool m_enableGuest = true;
    bool m_guestLoggedIn = false;

    // 内部辅助
    std::string SafeGetString(const nlohmann::json& obj, const std::string& key, const std::string& defaultVal = "");
    std::vector<std::string> ExtractTrackIds(const nlohmann::json& trackIdsJson);
};