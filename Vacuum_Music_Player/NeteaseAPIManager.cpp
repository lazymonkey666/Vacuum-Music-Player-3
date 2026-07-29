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

// ========== 通用请求 ==========
json NeteaseAPIManager::Request(const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& params,
    const httplib::Headers& extraHeaders) {
    if (m_baseUrl.empty()) throw std::runtime_error("API base URL not set");

    // 构造查询字符串（锁外）
    std::string query;
    for (const auto& p : params) {
        if (!query.empty()) query += "&";
        query += p.first + "=" + p.second;
    }
    std::string fullPath = path + (query.empty() ? "" : "?" + query);

    // 选择 Cookie（锁内小范围）
    std::string cookieToUse;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_userCookie.empty()) {
            cookieToUse = m_userCookie;
        }
        else if (m_guestLoggedIn && !m_guestCookie.empty()) {
            cookieToUse = m_guestCookie;
        }
    }

    // 将 Cookie 附加到 URL（或 Header）
    if (!cookieToUse.empty()) {
        // 对 cookie 值进行 URL 编码（假设有 encode_url 函数）
        fullPath += (fullPath.find('?') == std::string::npos ? "?" : "&");
        fullPath += "cookie=MUSIC_U=" + cookieToUse + ";os=pc;";
    }

    // 发起网络请求（锁外）
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
std::string NeteaseAPIManager::GetSongUrl(const std::string& songId, const std::string& level,const std::string cookie) {
    {
        auto resp = Request("/song/url/v1", { {"id", songId}, {"level", level} });
        if (resp.contains("code") && resp["code"].get<int>() != 200) return "";
        if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty()) {
            const auto& first = resp["data"][0];
            if (first.contains("url") && !first["url"].is_null() && first["url"].is_string())
                return first["url"].get<std::string>();
        }
        return "";
    }
    //else {
    //    //auto cookie_with_platform = "MUSIC_U=" + cookie + ";os=pc;";
    //    auto resp = Request("/song/url/v1", { {"id", songId}, {"level", level} ,{"cookie",cookie_with_platform} });
    //    if (resp.contains("code") && resp["code"].get<int>() != 200) return "";
    //    if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty()) {
    //        const auto& first = resp["data"][0];
    //        if (first.contains("url") && !first["url"].is_null() && first["url"].is_string())
    //            return first["url"].get<std::string>();
    //    }
    //    return "";
    //}
    
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
        resp = Request("/login/refresh", {}, {});
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
//工具函数 用于主程序调用存储cookie时加密和解密
bool EncryptStringWithDPAPI(const std::string& plaintext, std::string& ciphertext) {
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"AppCookie", nullptr, nullptr, nullptr, 0, &output))
        return false;

    ciphertext.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool DecryptStringWithDPAPI(const std::string& ciphertext, std::string& plaintext) {
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(ciphertext.data()));
    input.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
        return false;

    plaintext.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

std::string Base64Encode(const std::string& binary) {
    DWORD size = 0;
    CryptBinaryToStringA(
        reinterpret_cast<const BYTE*>(binary.data()),
        static_cast<DWORD>(binary.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr, &size
    );
    std::string result(size, 0);
    CryptBinaryToStringA(
        reinterpret_cast<const BYTE*>(binary.data()),
        static_cast<DWORD>(binary.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        result.data(), &size
    );
    if (!result.empty() && result.back() == '\0') result.pop_back(); // 移除结尾空字符
    return result;
}

std::string Base64Decode(const std::string& base64) {
    DWORD size = 0;
    CryptStringToBinaryA(
        base64.c_str(),
        static_cast<DWORD>(base64.size()),
        CRYPT_STRING_BASE64,
        nullptr, &size, nullptr, nullptr
    );
    std::string result(size, 0);
    CryptStringToBinaryA(
        base64.c_str(),
        static_cast<DWORD>(base64.size()),
        CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(result.data()), &size, nullptr, nullptr
    );
    return result;
}

bool NeteaseAPIManager::SendCaptcha(const std::string& phone) {
    try {
        auto resp = Request("/captcha/sent", { {"phone", phone} });
        return resp.contains("code") && resp["code"].get<int>() == 200;
    }
    catch (...) {
        return false;
    }
}

bool NeteaseAPIManager::LoginByCaptcha(const std::string& phone, const std::string& captcha) {
    try {
        auto resp = Request("/login/cellphone", { {"phone", phone}, {"captcha", captcha} });
        if (!resp.contains("code") || resp["code"].get<int>() != 200)
            return false;
        // 登录成功后，`Request` 内部已从 Set-Cookie 提取了 cookie 并保存到 m_userCookie
        // 但由于我们的 Request 函数并未自动保存到 m_userCookie，需要手动处理
        // 我们可以在 Request 中检测到登录接口时特殊处理，但更简单：在登录成功时主动调用 SetUserCookie
        // 由于 Request 没有返回 cookie，我们只能通过其他方式获取，但我们也可以直接使用响应中的 cookie 字段
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