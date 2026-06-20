#include "NeteaseAPIManager.h"
#define CPPHTTPLIB_USE_WIN32_FILE_IO 0
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

// ========== 单例 ==========
NeteaseAPIManager& NeteaseAPIManager::GetInstance() {
    static NeteaseAPIManager instance;
    return instance;
}

// ========== 配置 ==========
void NeteaseAPIManager::SetBaseURL(const std::string& url) {
    m_baseUrl = url;
    // 如果启用了游客登录，在首次调用时自动登录
    if (m_enableGuest && !m_guestLoggedIn) {
        LoginAsGuest();
    }
}

// ========== 游客登录 ==========
bool NeteaseAPIManager::LoginAsGuest() {
    if (m_baseUrl.empty()) return false;
    httplib::Client cli(m_baseUrl);
    auto res = cli.Get("/register/anonimous");
    if (res && res->status == 200) {
        auto it = res->headers.find("Set-Cookie");
        if (it != res->headers.end()) {
            std::string full = it->second;
            size_t pos = full.find(';');
            m_guestCookie = (pos != std::string::npos) ? full.substr(0, pos) : full;
            m_guestLoggedIn = true;
            return true;
        }
        // 如果 JSON 中有 cookie 字段
        try {
            auto jsonResp = json::parse(res->body);
            if (jsonResp.contains("cookie") && jsonResp["cookie"].is_string()) {
                m_guestCookie = jsonResp["cookie"].get<std::string>();
                m_guestLoggedIn = true;
                return true;
            }
        }
        catch (...) {}
    }
    return false;
}

// ========== 通用请求 ==========
json NeteaseAPIManager::Request(const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& params,
    const httplib::Headers& extraHeaders) {
    if (m_baseUrl.empty()) throw std::runtime_error("API base URL not set");

    httplib::Client cli(m_baseUrl);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(15, 0);

    std::string query;
    for (const auto& p : params) {
        if (!query.empty()) query += "&";
        query += p.first + "=" + p.second;
    }
    std::string fullPath = path + (query.empty() ? "" : "?" + query);

    httplib::Headers headers = extraHeaders;
    if (m_guestLoggedIn && !m_guestCookie.empty()) {
        headers.emplace("Cookie", m_guestCookie);
    }

    auto res = cli.Get(fullPath.c_str(), headers);
    if (!res) throw std::runtime_error("请求失败: 无响应");
    if (res->status != 200) throw std::runtime_error("HTTP " + std::to_string(res->status));

    try {
        return json::parse(res->body);
    }
    catch (...) {
        throw std::runtime_error("JSON 解析失败");
    }
}

// ========== 安全读取字符串 ==========
std::string NeteaseAPIManager::SafeGetString(const json& obj, const std::string& key, const std::string& defaultVal) {
    if (obj.contains(key) && !obj[key].is_null()) {
        if (obj[key].is_string()) return obj[key].get<std::string>();
        if (obj[key].is_number()) return std::to_string(obj[key].get<int64_t>());
    }
    return defaultVal;
}

// ========== 提取 trackIds ==========
std::vector<std::string> NeteaseAPIManager::ExtractTrackIds(const json& trackIdsJson) {
    std::vector<std::string> ids;
    if (!trackIdsJson.is_array()) return ids;
    for (const auto& item : trackIdsJson) {
        if (item.is_number()) {
            ids.push_back(std::to_string(item.get<int64_t>()));
        }
        else if (item.is_object() && item.contains("id")) {
            ids.push_back(SafeGetString(item, "id"));
        }
        else if (item.is_string()) {
            ids.push_back(item.get<std::string>());
        }
    }
    return ids;
}

// ========== 获取歌单 trackIds ==========
std::vector<std::string> NeteaseAPIManager::GetPlaylistTrackIds(int64_t playlistId) {
    auto resp = Request("/playlist/detail", { {"id", std::to_string(playlistId)} });
    if (resp.contains("code") && resp["code"].get<int>() != 200)
        throw std::runtime_error("API error: " + std::to_string(resp["code"].get<int>()));
    if (!resp.contains("playlist") || !resp["playlist"].is_object())
        throw std::runtime_error("Invalid playlist response");
    const auto& pl = resp["playlist"];
    if (pl.contains("trackIds") && pl["trackIds"].is_array()) {
        return ExtractTrackIds(pl["trackIds"]);
    }
    return {};
}

// ========== 批量获取歌曲详情 ==========
std::vector<NeteaseSongInfo> NeteaseAPIManager::GetSongsDetail(const std::vector<std::string>& ids) {
    if (ids.empty()) return {};
    std::string idStr;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) idStr += ",";
        idStr += ids[i];
    }
    auto resp = Request("/song/detail", { {"ids", idStr} });
    if (resp.contains("code") && resp["code"].get<int>() != 200)
        throw std::runtime_error("API error: " + std::to_string(resp["code"].get<int>()));
    if (!resp.contains("songs") || !resp["songs"].is_array())
        throw std::runtime_error("Missing songs array");

    std::vector<NeteaseSongInfo> songs;
    for (const auto& item : resp["songs"]) {
        NeteaseSongInfo s;
        s.id = SafeGetString(item, "id");
        s.name = SafeGetString(item, "name", "未知歌曲");
        s.duration = item.value("dt", 0);
        s.fee = item.value("fee", 0);
        if (item.contains("ar") && item["ar"].is_array()) {
            for (const auto& ar : item["ar"]) {
                std::string name = SafeGetString(ar, "name");
                if (!name.empty()) s.artists.push_back(name);
            }
        }
        if (item.contains("al") && item["al"].is_object()) {
            s.album = SafeGetString(item["al"], "name", "未知专辑");
            s.albumArtUrl = SafeGetString(item["al"], "picUrl");
        }
        songs.push_back(s);
    }
    return songs;
}

// ========== 获取单首歌曲 URL ==========
std::string NeteaseAPIManager::GetSongUrl(const std::string& songId, const std::string& level) {
    auto resp = Request("/song/url/v1", { {"id", songId}, {"level", level} });
    if (resp.contains("code") && resp["code"].get<int>() != 200) return "";
    if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty()) {
        const auto& first = resp["data"][0];
        if (first.contains("url") && !first["url"].is_null() && first["url"].is_string())
            return first["url"].get<std::string>();
    }
    return "";
}

// ========== 获取歌曲完整信息（含 URL） ==========
NeteaseSongInfo NeteaseAPIManager::GetSongFullInfo(const std::string& songId, const std::string& level) {
    auto infos = GetSongsDetail({ songId });
    if (infos.empty()) return NeteaseSongInfo();
    auto& info = infos[0];
    // 尝试获取 URL（可能为试听或完整）
    info.playUrl = GetSongUrl(songId, level);
    // 如果返回空，尝试获取试听链接（但API没有独立试听链接，若版权限制可能返回空或试听）
    // 我们不再额外区分，因为 API 返回的就是当前权限下的链接
    return info;
}

// ========== 搜索歌曲 ==========
std::vector<NeteaseSongInfo> NeteaseAPIManager::SearchSongs(const std::string& keyword, int limit) {
    auto resp = Request("/search", { {"keywords", keyword}, {"limit", std::to_string(limit)} });
    if (resp.contains("code") && resp["code"].get<int>() != 200)
        throw std::runtime_error("API error: " + std::to_string(resp["code"].get<int>()));
    if (!resp.contains("result") || !resp["result"].contains("songs"))
        return {};
    std::vector<NeteaseSongInfo> songs;
    for (const auto& item : resp["result"]["songs"]) {
        NeteaseSongInfo s;
        s.id = SafeGetString(item, "id");
        s.name = SafeGetString(item, "name", "未知歌曲");
        s.duration = item.value("duration", 0);
        s.fee = item.value("fee", 0);
        if (item.contains("artists") && item["artists"].is_array()) {
            for (const auto& ar : item["artists"]) {
                std::string name = SafeGetString(ar, "name");
                if (!name.empty()) s.artists.push_back(name);
            }
        }
        if (item.contains("album") && item["album"].is_object()) {
            s.album = SafeGetString(item["album"], "name", "未知专辑");
        }
        songs.push_back(s);
    }
    return songs;
}

std::string NeteaseAPIManager::GetLyrics(const std::string& songId) {
    auto resp = Request("/lyric", { {"id", songId} }, {});
    if (resp.contains("code") && resp["code"].get<int>() == 200) {
        if (resp.contains("lrc") && resp["lrc"].is_object()) {
            auto& lrc = resp["lrc"];
            if (lrc.contains("lyric") && lrc["lyric"].is_string()) {
                return lrc["lyric"].get<std::string>();
            }
        }
    }
    return "";
}