#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "NeteaseAPIManager.h"
#define CPPHTTPLIB_USE_WIN32_FILE_IO 0
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

// ========== 轻量级 URL 编码辅助函数 ==========
static std::string UrlEncode(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        // 保留字母、数字和特定安全字符，其余全部转义
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped.push_back(c);
        }
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            escaped.append(buf);
        }
    }
    return escaped;
}

// ═══════════════════════════════════════════════════════════
// JSON 字段安全读取（int64 防溢出 / 字符串空值兜底）
// ═══════════════════════════════════════════════════════════
static int64_t JInt64(const json& o, const char* k, int64_t def = 0) {
    if (o.contains(k) && o[k].is_number()) return o[k].get<int64_t>();
    return def;
}

static std::string JStr(const json& o, const char* k, const char* def = "") {
    if (o.contains(k) && o[k].is_string()) return o[k].get<std::string>();
    return def;
}


// ========== 单例 ==========
NeteaseAPIManager& NeteaseAPIManager::GetInstance() {
    static NeteaseAPIManager instance;
    return instance;
}

// ========== 配置 ==========
void NeteaseAPIManager::SetBaseURL(const std::string& url) {
    m_baseUrl = url;
    // 只在没有用户 Cookie 且启用游客功能时自动登录游客
    if (m_enableGuest && m_userCookie.empty() && !m_guestLoggedIn) {
        LoginAsGuest();
    }
}




// ========== 游客登录 ==========
bool NeteaseAPIManager::LoginAsGuest() {
    if (m_baseUrl.empty()) return false;
    httplib::Client cli(m_baseUrl);
    auto res = cli.Get("/register/anonimous");
    if (res && res->status == 200) {
        std::string cookie;
        auto it = res->headers.find("Set-Cookie");
        if (it != res->headers.end()) {
            std::string full = it->second;
            size_t pos = full.find(';');
            cookie = (pos != std::string::npos) ? full.substr(0, pos) : full;
        }
        // 若从 headers 未获取到，尝试从 JSON body 提取
        if (cookie.empty()) {
            try {
                auto jsonResp = json::parse(res->body);
                if (jsonResp.contains("cookie") && jsonResp["cookie"].is_string())
                    cookie = jsonResp["cookie"].get<std::string>();
            }
            catch (...) {}
        }
        if (!cookie.empty()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_guestCookie = cookie;
            m_guestLoggedIn = true;
            return true;
        }
    }
    return false;
}

// ========== 单个源发请求（内部函数）==========
json NeteaseAPIManager::DoRequest(ApiSource source,
    const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& params,
    bool needUserCookie)
{
    // 构造 query
    std::string query;
    for (const auto& p : params) {
        if (!query.empty()) query += "&";

        // ★ 1. 自动适配官方与第三方对 "ids" 格式的不同要求
        if (source == ApiSource::Official && p.first == "ids") {
            // 官方要求 JSON 数组格式：[1,2,3]，并必须 URL 编码
            std::string val = "[" + p.second + "]";
            // ✅ 改为调用自定义的编码函数
            query += p.first + "=" + UrlEncode(val);
        }
        else {
            // 第三方直接传 1,2,3
            query += p.first + "=" + p.second;
        }
    }
    std::string fullPath = path + (query.empty() ? "" : "?" + query);

    std::string host;
    httplib::Headers headers;

    if (source == ApiSource::Official) {
        host = "https://music.163.com";
        headers.emplace("User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; WOW64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) "
            "Safari/537.36 Chrome/91.0.4472.164 "
            "NeteaseMusicDesktop/2.10.2.200154");
        headers.emplace("Referer", "https://music.163.com");
        headers.emplace("Cookie", "os=pc; appver=; osver=; deviceId=pyncm!");
    }
    else {
        if (m_baseUrl.empty())
            throw std::runtime_error("ThirdParty base URL not set");
        host = m_baseUrl;
        headers.emplace("User-Agent", "VacuumMusicPlayer/3.0");
        if (needUserCookie) {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::string cookie = !m_userCookie.empty() ? m_userCookie : m_guestCookie;
            if (!cookie.empty()) {
                headers.emplace("Cookie", "MUSIC_U=" + cookie + "; os=pc;");
            }
        }
    }

    httplib::Client cli(host);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(15, 0);

    auto res = cli.Get(fullPath.c_str(), headers);
    if (!res) throw std::runtime_error("no response");
    if (res->status != 200) throw std::runtime_error("HTTP " + std::to_string(res->status));

    json j = json::parse(res->body);

    if (j.contains("code") && j["code"].is_number()) {
        int code = j["code"].get<int>();
        if (code != 200 && code != 0) {
            throw std::runtime_error("business code " + std::to_string(code)
                + ": " + j.value("message", ""));
        }
    }
    return j;
}

// ========== 主备降级路由 ==========
json NeteaseAPIManager::RequestWithRoute(
    ApiSource primary,
    const std::string& officialPath,
    const std::string& thirdPartyPath,
    const std::vector<std::pair<std::string, std::string>>& params,
    bool needUserCookie)
{
    // 决策：哪个先试，哪个后试
    ApiSource secondary = (primary == ApiSource::Official)
        ? ApiSource::ThirdParty : ApiSource::Official;

    auto trySource = [&](ApiSource src) -> std::optional<json> {
        CircuitBreaker& circuit = (src == ApiSource::Official)
            ? m_officialCircuit : m_thirdPartyCircuit;
        RateLimiter& limiter = (src == ApiSource::Official)
            ? m_officialLimiter : m_thirdPartyLimiter;

        // 熔断中 → 直接跳过
        if (circuit.IsInCooldown()) {
            OutputDebugStringA(("[Route] " + std::string(src == ApiSource::Official ? "Official" : "ThirdParty")
                + " in cooldown, skip\n").c_str());
            return std::nullopt;
        }

        // 限流命中 → 跳过（不阻塞）
        if (!limiter.TryAcquire()) {
            OutputDebugStringA(("[Route] " + std::string(src == ApiSource::Official ? "Official" : "ThirdParty")
                + " rate limited, skip\n").c_str());
            return std::nullopt;
        }

        try {
            const std::string& path = (src == ApiSource::Official)
                ? officialPath : thirdPartyPath;
            json result = DoRequest(src, path, params, needUserCookie);
            circuit.OnSuccess();
            OutputDebugStringA(("[Route] " + std::string(src == ApiSource::Official ? "Official" : "ThirdParty")
                + " OK: " + path + "\n").c_str());
            return result;
        }
        catch (const std::exception& e) {
            circuit.OnFailure();
            OutputDebugStringA(("[Route] " + std::string(src == ApiSource::Official ? "Official" : "ThirdParty")
                + " FAIL: " + e.what() + "\n").c_str());
            return std::nullopt;
        }
        };

    // 先试主源
    if (auto r = trySource(primary)) return *r;

    // 主源失败 → 试备源
    if (auto r = trySource(secondary)) return *r;

    // 两源全挂
    throw std::runtime_error("both sources failed for " + officialPath);
}

// ========== 兼容重载函数 ==========
json NeteaseAPIManager::RequestThidParty(const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& params,
    const httplib::Headers& extraHeaders) {
    if (m_baseUrl.empty()) throw std::runtime_error("API base URL not set");

    // 构造 query
    std::string query;
    for (const auto& p : params) {
        if (!query.empty()) query += "&";
        query += p.first + "=" + p.second;
    }
    std::string fullPath = path + (query.empty() ? "" : "?" + query);

    // 携带 Cookie（登录/刷新等必须）
    std::string cookieToUse;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_userCookie.empty()) cookieToUse = m_userCookie;
        else if (m_guestLoggedIn && !m_guestCookie.empty()) cookieToUse = m_guestCookie;
    }
    if (!cookieToUse.empty()) {
        fullPath += (fullPath.find('?') == std::string::npos ? "?" : "&");
        fullPath += "cookie=MUSIC_U=" + cookieToUse + ";os=pc;";
    }

    httplib::Client cli(m_baseUrl);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(15, 0);
    httplib::Headers headers = extraHeaders;
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

// ========== 通用请求 ==========
json NeteaseAPIManager::Request(const std::string& officialPath,
    const std::string& thirdPartyPath,
    const std::vector<std::pair<std::string, std::string>>& params,
    ApiSource primary,
    bool needUserCookie)
{
    ApiSource secondary = (primary == ApiSource::Official)
        ? ApiSource::ThirdParty : ApiSource::Official;

    auto trySource = [&](ApiSource src) -> std::optional<json> {
        CircuitBreaker& circuit = (src == ApiSource::Official)
            ? m_officialCircuit : m_thirdPartyCircuit;
        RateLimiter& limiter = (src == ApiSource::Official)
            ? m_officialLimiter : m_thirdPartyLimiter;
        const char* name = (src == ApiSource::Official) ? "Official" : "ThirdParty";

        // 熔断中跳过
        if (circuit.IsInCooldown()) {
            OutputDebugStringA(("[Route] " + std::string(name) + " cooldown, skip\n").c_str());
            return std::nullopt;
        }
        // 限流命中跳过
        if (!limiter.TryAcquire()) {
            OutputDebugStringA(("[Route] " + std::string(name) + " rate limited, skip\n").c_str());
            return std::nullopt;
        }

        try {
            const std::string& path = (src == ApiSource::Official) ? officialPath : thirdPartyPath;
            json r = DoRequest(src, path, params, needUserCookie);
            circuit.OnSuccess();
            OutputDebugStringA(("[Route] " + std::string(name) + " OK: " + path + "\n").c_str());
            return r;
        }
        catch (const std::exception& e) {
            circuit.OnFailure();
            OutputDebugStringA(("[Route] " + std::string(name) + " FAIL: " + e.what() + "\n").c_str());
            return std::nullopt;
        }
        };

    if (auto r = trySource(primary))   return *r;
    if (auto r = trySource(secondary)) return *r;

    throw std::runtime_error("both sources failed: " + officialPath);
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
    // ★ 第三方优先（私密歌单官方拿不到）
    auto resp = RequestWithRoute(
        ApiSource::ThirdParty,          // ← 主源改第三方
        "/api/playlist/detail",
        "/playlist/detail",
        { {"id", std::to_string(playlistId)} },
        true);                          // ★ 第三方需要用户 Cookie（私密歌单）

    // 业务错误码检测
    if (resp.contains("code") && resp["code"].is_number()) {
        int code = resp["code"].get<int>();
        if (code != 200 && code != 0) {
            throw std::runtime_error("playlist API failed: code=" +
                std::to_string(code) + " msg=" + resp.value("msg", ""));
        }
    }

    // 兼容两种结构：result.playlist / playlist
    const json* plPtr = nullptr;
    if (resp.contains("result") && resp["result"].is_object()
        && resp["result"].contains("playlist") && resp["result"]["playlist"].is_object()) {
        plPtr = &resp["result"]["playlist"];
    }
    else if (resp.contains("playlist") && resp["playlist"].is_object()) {
        plPtr = &resp["playlist"];
    }

    if (!plPtr) {
        throw std::runtime_error("playlist detail: no valid playlist in response");
    }

    const auto& pl = *plPtr;
    if (pl.contains("trackIds") && pl["trackIds"].is_array()) {
        return ExtractTrackIds(pl["trackIds"]);
    }
    return {};
}

// ========== 批量获取歌曲详情 ==========
std::vector<NeteaseSongInfo> NeteaseAPIManager::GetSongsDetail(const std::vector<std::string>& ids) {
    if (ids.empty()) return {};

    const size_t batchSize = 50;
    std::vector<NeteaseSongInfo> songs;

    for (size_t offset = 0; offset < ids.size(); offset += batchSize) {
        size_t end = std::min(offset + batchSize, ids.size());
        std::string idStr;
        for (size_t i = offset; i < end; ++i) {
            if (!idStr.empty()) idStr += ",";
            idStr += ids[i];
        }

        // ★ 官方要 ids=[id1,id2] 带方括号
        std::string idsParam = idStr;

        auto resp = RequestWithRoute(
            ApiSource::Official,            // ★ 官方优先
            "/api/song/detail",
            "/song/detail",
            { {"ids", idsParam} },
            false);

        if (!resp.contains("songs") || !resp["songs"].is_array()) {
            OutputDebugStringA("[GetSongsDetail] missing songs array\n");
            continue;
        }

        for (const auto& item : resp["songs"]) {
            NeteaseSongInfo s;
            s.id = SafeGetString(item, "id");
            s.name = SafeGetString(item, "name", "未知歌曲");

            // ★ 兼容时长 (官方是 duration，第三方是 dt)
            if (item.contains("duration")) {
                s.duration = item.value("duration", 0);
            }
            else if (item.contains("dt")) {
                s.duration = item.value("dt", 0);
            }

            s.fee = item.value("fee", 0);

            // ★ 兼容歌手 (官方是 artists，第三方是 ar)
            std::string artistsKey = item.contains("artists") ? "artists" : "ar";
            if (item.contains(artistsKey) && item[artistsKey].is_array()) {
                for (const auto& ar : item[artistsKey]) {
                    std::string name = SafeGetString(ar, "name");
                    if (!name.empty()) s.artists.push_back(name);
                }
            }

            // ★ 兼容专辑 (官方是 album，第三方是 al)
            std::string albumKey = item.contains("album") ? "album" : "al";
            if (item.contains(albumKey) && item[albumKey].is_object()) {
                s.album = SafeGetString(item[albumKey], "name", "未知专辑");
                s.albumArtUrl = SafeGetString(item[albumKey], "picUrl");
            }

            songs.push_back(s);
        }

        if (offset + batchSize < ids.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    return songs;
}


// ========== 获取单首歌曲 URL ==========
std::string NeteaseAPIManager::GetSongUrl(const std::string& songId, const std::string& level, const std::string& cookie) {
    try {
        auto resp = Request("/api/song/url/v1", "/song/url/v1",
            { {"id", songId}, {"level", level} },
            ApiSource::ThirdParty, true);

        if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty()) {
            const auto& first = resp["data"][0];
            if (first.contains("url") && !first["url"].is_null() && first["url"].is_string()) {
                return first["url"].get<std::string>();
            }
        }
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("[GetSongUrl] 异常: " + std::string(e.what()) + "\n").c_str());
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
    auto resp = Request("/api/cloudsearch/pc", "/search",
        { {"s", keyword}, {"type","1"},
          {"limit", std::to_string(limit)}, {"offset","0"} },
        ApiSource::Official, false);    if (resp.contains("code") && resp["code"].get<int>() != 200)
        throw std::runtime_error("API error: " + std::to_string(resp["code"].get<int>()));
    if (!resp.contains("result") || !resp["result"].contains("songs"))
        return {};
    std::vector<NeteaseSongInfo> songs;
    for (const auto& item : resp["result"]["songs"]) {
        NeteaseSongInfo s;
        s.id = SafeGetString(item, "id");
        s.name = SafeGetString(item, "name", "未知歌曲");

        // 兼容时长
        if (item.contains("duration")) {
            s.duration = item.value("duration", 0);
        }
        else if (item.contains("dt")) {
            s.duration = item.value("dt", 0);
        }

        s.fee = item.value("fee", 0);

        // 兼容歌手
        std::string artistsKey = item.contains("artists") ? "artists" : "ar";
        if (item.contains(artistsKey) && item[artistsKey].is_array()) {
            for (const auto& ar : item[artistsKey]) {
                std::string name = SafeGetString(ar, "name");
                if (!name.empty()) s.artists.push_back(name);
            }
        }

        // 兼容专辑
        std::string albumKey = item.contains("album") ? "album" : "al";
        if (item.contains(albumKey) && item[albumKey].is_object()) {
            s.album = SafeGetString(item[albumKey], "name", "未知专辑");
            s.albumArtUrl = SafeGetString(item[albumKey], "picUrl");
        }

        songs.push_back(s);
    }
    return songs;
}

std::string NeteaseAPIManager::GetLyrics(const std::string& songId) {
    // ★ 官方优先（匿名可拿）
    auto resp = RequestWithRoute(
        ApiSource::Official,            // ★ 官方优先
        "/api/song/lyric",
        "/lyric",
        {
            {"os", "pc"},
            {"id", songId},
            {"lv", "-1"},
            {"kv", "-1"},
            {"tv", "-1"}
        },
        false);

    // 官方返回：lrc.lyric（原文）
    if (resp.contains("lrc") && resp["lrc"].is_object()
        && resp["lrc"].contains("lyric") && resp["lrc"]["lyric"].is_string()) {
        std::string lyric = resp["lrc"]["lyric"].get<std::string>();
        if (!lyric.empty()) return lyric;
    }

    return "";
}

static bool HasError(const json& resp) {
    return resp.contains("_error") && resp["_error"].is_string();
}

static std::string GetError(const json& resp) {
    return HasError(resp) ? resp["_error"].get<std::string>() : "";
}

bool NeteaseAPIManager::UpdateCookie(const std::string& cookie) {
    // 1. 处理传入的cookie（加锁）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!cookie.empty()) {
            m_userCookie = cookie;
        }
    }

    // 2. 读取当前状态（加锁）
    bool isUser;
    std::string currentCookie;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        isUser = !m_userCookie.empty();
        currentCookie = isUser ? m_userCookie : m_guestCookie;
    }

    // 3. 若无有效cookie，尝试游客登录（在锁外）
    if (currentCookie.empty()) {
        if (m_enableGuest && !m_guestLoggedIn) {
            LoginAsGuest();
        }
        // 再次检查是否有cookie
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            currentCookie = !m_userCookie.empty() ? m_userCookie : m_guestCookie;
        }
        if (currentCookie.empty()) {
            return false;
        }
    }

    // 4. 调用刷新接口（锁外，因为Request内部会加锁）
    json resp;
    try {
        resp = RequestThidParty("/login/refresh", {}, {});
    }
    catch (...) {
        return false;
    }

    // 5. 处理响应（加锁）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (HasError(resp) || !resp.contains("code")) return false;
        int code = resp["code"].get<int>();

        if (code == 200) {
            // 提取新cookie并更新
            if (resp.contains("cookie") && resp["cookie"].is_string()) {
                std::string newCookie = resp["cookie"].get<std::string>();
                std::string valueOnly;
                size_t pos = newCookie.find("MUSIC_U=");
                if (pos != std::string::npos) {
                    size_t start = pos + 8;
                    size_t end = newCookie.find(';', start);
                    valueOnly = (end == std::string::npos) ? newCookie.substr(start) : newCookie.substr(start, end - start);
                    // 校验十六进制
                    bool allHex = true;
                    for (char c : valueOnly) {
                        if (!std::isxdigit(static_cast<unsigned char>(c))) {
                            allHex = false;
                            break;
                        }
                    }
                    if (allHex && !valueOnly.empty()) {
                        if (isUser) {
                            m_userCookie = valueOnly;
                        }
                        else {
                            m_guestCookie = valueOnly;
                            m_guestLoggedIn = true;
                        }
                        return true;
                    }
                }
            }
            return false;
        }
        else if (code == 301) {
            // 登录过期，清除对应的cookie，游客会自动启用（下一次Request会尝试游客）
            if (isUser) {
                m_userCookie.clear();
            }
            else {
                m_guestCookie.clear();
                m_guestLoggedIn = false;
            }
            return false;
        }
        else {
            return false;
        }
    }
}

std::string NeteaseAPIManager::GetCurrentCookie() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_userCookie.empty() ? m_userCookie : m_guestCookie;
}

bool NeteaseAPIManager::SendCaptcha(const std::string& phone) {
    try {
        auto resp = RequestThidParty("/captcha/sent", { {"phone", phone} });
        return resp.contains("code") && resp["code"].get<int>() == 200;
    }
    catch (...) {
        return false;
    }
}

bool NeteaseAPIManager::LoginByCaptcha(const std::string& phone, const std::string& captcha) {
    try {
        auto resp = RequestThidParty("/login/cellphone", { {"phone", phone}, {"captcha", captcha} });
        if (!resp.contains("code") || resp["code"].get<int>() != 200)
            return false;
        // 登录成功后，`Request` 内部已从 Set-Cookie 提取了 cookie 并保存到 m_userCookie
        // 但由于我们的 RequestThidParty 函数并未自动保存到 m_userCookie，需要手动处理
        // 我们可以在 RequestThidParty 中检测到登录接口时特殊处理，但更简单：在登录成功时主动调用 SetUserCookie
        // 由于 RequestThidParty 没有返回 cookie，我们只能通过其他方式获取，但我们也可以直接使用响应中的 cookie 字段
        if (resp.contains("cookie") && resp["cookie"].is_string()) {
            std::string fullCookie = resp["cookie"].get<std::string>();
            // 提取 MUSIC_U 的值
            size_t pos = fullCookie.find("MUSIC_U=");
            if (pos != std::string::npos) {
                size_t start = pos + 8;
                size_t end = fullCookie.find(';', start);
                std::string value = (end == std::string::npos) ? fullCookie.substr(start) : fullCookie.substr(start, end - start);
                if (!value.empty()) {
                    SetUserCookie(value);
                    return true;
                }
            }
        }
        return false;
    }
    catch (...) {
        return false;
    }
}

void NeteaseAPIManager::SetUserCookie(const std::string& cookie) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userCookie = cookie;
}

void NeteaseAPIManager::ClearUserCookie() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_userCookie.clear();
    }
    // 锁已释放，此时调用 LoginAsGuest（内部会加锁）不会死锁
    if (m_enableGuest && !m_guestLoggedIn) {
        LoginAsGuest();
    }
}
std::string NeteaseAPIManager::GetNeteaseUserName() {
    try {
        auto resp = RequestThidParty("/user/account", {});
        if (resp.contains("code") && resp["code"].get<int>() == 200) {
            if (resp.contains("profile") && resp["profile"].contains("nickname")) {
                return resp["profile"]["nickname"].get<std::string>();
            }
        }
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("获取用户名失败: " + std::string(e.what()) + "\n").c_str());
    }
    return "";
}