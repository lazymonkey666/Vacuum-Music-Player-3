#include "pch.h"
#include "AMLLWebSocket.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <iomanip>
#include <chrono>
#include <thread>
#include <ixwebsocket/IXNetSystem.h>

#include <string>
#include <vector>
#include <cstdint>

extern HWND g_hWnd;

//base64编码辅助函数
std::string AMLLWebSocketClient::base64_encode(const std::vector<uint8_t>& data) {
    const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    uint32_t octet_a = 0, octet_b = 0, octet_c = 0;
    for (size_t n = 0; n < data.size(); n += 3) {
        octet_a = (n < data.size()) ? data[n] : 0;
        octet_b = (n + 1 < data.size()) ? data[n + 1] : 0;
        octet_c = (n + 2 < data.size()) ? data[n + 2] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result.push_back(base64_chars[(triple >> 18) & 0x3F]);
        result.push_back(base64_chars[(triple >> 12) & 0x3F]);
        result.push_back(base64_chars[(triple >> 6) & 0x3F]);
        result.push_back(base64_chars[triple & 0x3F]);
    }

    // 处理填充
    size_t padding = (3 - data.size() % 3) % 3;
    for (size_t p = 0; p < padding; ++p) {
        result[result.length() - 1 - p] = '=';
    }
    return result;
}

// ==================== 静态序列化函数实现 ====================
std::vector<uint8_t> AMLLWebSocketClient::serializeSetMusicInfo(
    const std::string& music_id,
    const std::string& music_name,
    const std::string& album_id,
    const std::string& album_name,
    const std::vector<Artist>& artists,
    uint64_t duration) {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::SetMusicInfo);
    write_nullstr(buf, music_id);
    write_nullstr(buf, music_name);
    write_nullstr(buf, album_id);
    write_nullstr(buf, album_name);
    write_u32(buf, (uint32_t)artists.size());
    for (const auto& a : artists) {
        write_nullstr(buf, a.id);
        write_nullstr(buf, a.name);
    }
    write_u64(buf, duration);
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializeSetAlbumCoverData(const std::vector<uint8_t>& image_data) {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::SetMusicAlbumCoverImageData);
    write_u32(buf, (uint32_t)image_data.size());
    buf.insert(buf.end(), image_data.begin(), image_data.end());
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializeSetLyricFromTTML(const std::string& ttmlXml) {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::SetLyricFromTTML);
    write_nullstr(buf, ttmlXml);
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializeOnResumed() {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::OnResumed);
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializeOnPaused() {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::OnPaused);
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializeOnPlayProgress(uint64_t progress) {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::OnPlayProgress);
    write_u64(buf, progress);
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializePong() {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::Pong);
    return buf;
}

std::vector<uint8_t> AMLLWebSocketClient::serializeSetAlbumCoverURI(const std::string& uri) {
    std::vector<uint8_t> buf;
    write_u16(buf, (uint16_t)MsgType::SetMusicAlbumCoverImageURI);
    write_nullstr(buf, uri);
    return buf;
}

std::string AMLLWebSocketClient::imageToDataURI(const std::vector<uint8_t>& imageData, const std::string& mimeType) {
    // 例如 mimeType = "image/png" 或 "image/jpeg"
    std::string base64 = base64_encode(imageData);
    return "data:" + mimeType + ";base64," + base64;
}

// ==================== LRC 转换工具函数 ====================
double AMLLWebSocketClient::parseLrcTime(const std::string& tag) {
    std::regex re(R"(\[(\d{2}):(\d{2})(?:\.(\d{2}))?\])");
    std::smatch m;
    if (std::regex_search(tag, m, re)) {
        int minutes = std::stoi(m[1]);
        int seconds = std::stoi(m[2]);
        int centiseconds = m.size() > 3 && m[3].matched ? std::stoi(m[3]) : 0;
        return minutes * 60.0 + seconds + centiseconds / 100.0;
    }
    return 0.0;
}



// 仅这一个函数，无任何外部依赖，直接嵌入你的类中
std::string AMLLWebSocketClient::convertLRCToTTML(const std::string& lrc)
{
    std::string title, artist, album, creator;
    std::vector<std::pair<double, std::string>> lyrics;

    // 1. 按行分割
    std::vector<std::string> lines;
    std::stringstream ss(lrc);
    std::string line;
    while (std::getline(ss, line))
    {
        lines.push_back(line);
    }

    // 2. 逐行解析LRC
    for (const auto& l : lines)
    {
        // 内置去首尾空白（不单独写trim函数）
        size_t sStart = l.find_first_not_of(" \t\n\r");
        size_t sEnd = l.find_last_not_of(" \t\n\r");
        std::string trimLine;
        if (sStart != std::string::npos)
            trimLine = l.substr(sStart, sEnd - sStart + 1);
        else
            trimLine = "";

        if (trimLine.empty()) continue;

        // 判断是否为元数据行 [ti:xxx] [ar:xxx] 排除纯时间戳[00:00.00]
        bool isMetadata = false;
        if (trimLine.front() == '[' && trimLine.back() == ']')
        {
            for (char c : trimLine)
            {
                if (std::isalpha(static_cast<unsigned char>(c)))
                {
                    isMetadata = true;
                    break;
                }
            }
        }

        // 解析元数据
        if (isMetadata)
        {
            size_t colonPos = trimLine.find(':');
            if (colonPos == std::string::npos) continue;

            // 解析key并去空格
            std::string keyRaw = trimLine.substr(1, colonPos - 1);
            size_t kS = keyRaw.find_first_not_of(" \t");
            size_t kE = keyRaw.find_last_not_of(" \t");
            std::string key = (kS != std::string::npos) ? keyRaw.substr(kS, kE - kS + 1) : "";

            // 解析value并去空格
            std::string valRaw = trimLine.substr(colonPos + 1, trimLine.size() - colonPos - 2);
            size_t vS = valRaw.find_first_not_of(" \t");
            size_t vE = valRaw.find_last_not_of(" \t");
            std::string value = (vS != std::string::npos) ? valRaw.substr(vS, vE - vS + 1) : "";

            if (key == "ti") title = value;
            else if (key == "ar") artist = value;
            else if (key == "al") album = value;
            else if (key == "by") creator = value;

            continue;
        }

        // 解析多行时间戳歌词
        size_t pos = 0;
        while (pos < trimLine.size() && trimLine[pos] == '[')
        {
            size_t endBracket = trimLine.find(']', pos + 1);
            if (endBracket == std::string::npos) break;

            std::string timeStr = trimLine.substr(pos + 1, endBracket - pos - 1);
            double time = 0.0;
            bool timeValid = false;

            size_t colon = timeStr.find(':');
            size_t dot = timeStr.find('.');
            if (colon != std::string::npos && dot != std::string::npos)
            {
                try
                {
                    int min = std::stoi(timeStr.substr(0, colon));
                    int sec = std::stoi(timeStr.substr(colon + 1, dot - colon - 1));
                    int cent = std::stoi(timeStr.substr(dot + 1));
                    time = min * 60.0 + sec + cent / 100.0;
                    timeValid = true;
                }
                catch (...) {}
            }
            if (!timeValid) break;

            // 取歌词文本并去首尾空白
            std::string textRaw = trimLine.substr(endBracket + 1);
            size_t tS = textRaw.find_first_not_of(" \t");
            size_t tE = textRaw.find_last_not_of(" \t");
            std::string text;
            if (tS != std::string::npos)
                text = textRaw.substr(tS, tE - tS + 1);
            else
                text = "";

            // 保留空歌词行（纯时间戳间隔）
            lyrics.emplace_back(time, text);
            pos = endBracket + 1;
        }
    }

    // 按时间升序排序
    std::sort(lyrics.begin(), lyrics.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
        });

    // 时间格式化 内联lambda
    auto formatTime = [](double seconds) -> std::string {
        int totalSec = static_cast<int>(seconds);
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        int s = totalSec % 60;
        int ms = static_cast<int>((seconds - totalSec) * 1000);

        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << h << ":"
            << std::setw(2) << std::setfill('0') << m << ":"
            << std::setw(2) << std::setfill('0') << s << "."
            << std::setw(3) << std::setfill('0') << ms;
        return oss.str();
        };

    // 生成TTML 无缝衔接
    std::ostringstream ttml;
    ttml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ttml << "<tt xmlns=\"http://www.w3.org/ns/ttml\" xml:lang=\"zh-CN\">\n";
    ttml << "  <head><metadata>\n";
    if (!title.empty())  ttml << "      <title>" << title << "</title>\n";
    if (!artist.empty()) ttml << "      <artist>" << artist << "</artist>\n";
    if (!album.empty())  ttml << "      <album>" << album << "</album>\n";
    if (!creator.empty()) ttml << "      <creator>" << creator << "</creator>\n";
    ttml << "    </metadata></head>\n  <body><div>\n";

    for (size_t i = 0; i < lyrics.size(); ++i)
    {
        std::string begin = formatTime(lyrics[i].first);
        if (i < lyrics.size() - 1)
        {
            std::string end = formatTime(lyrics[i + 1].first);
            ttml << "      <p begin=\"" << begin << "\" end=\"" << end << "\">" << lyrics[i].second << "</p>\n";
        }
        else
        {
            ttml << "      <p begin=\"" << begin << "\" end=\"" << "99:59:59.999" << "\">" << lyrics[i].second << "</p>\n";
        }
    }

    ttml << "    </div></body>\n</tt>";
    return ttml.str();
}
uint64_t AMLLWebSocketClient::getPendingSeek() {
    return PendingSeek.exchange(-1);   // 原子地读取并清零
}
// ==================== 类成员函数实现 ====================
AMLLWebSocketClient::AMLLWebSocketClient(const std::string& url)
    : url_(url), connected_(false), running_(true) ,PendingSeek(-1){
    ix::initNetSystem();
    webSocket_.setUrl(url_);
    webSocket_.setPingInterval(45);
    webSocket_.enableAutomaticReconnection();
    webSocket_.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& msg) {
            onMessage(msg);
        });
}

AMLLWebSocketClient::~AMLLWebSocketClient() {
    stop();
    ix::uninitNetSystem();
}

void AMLLWebSocketClient::start() {
    webSocket_.start();
}

void AMLLWebSocketClient::stop() {
    running_ = false;
    if (progressThread_ && progressThread_->joinable())
        progressThread_->join();
    webSocket_.stop();
}

void AMLLWebSocketClient::waitForExit() {
    std::cout << "\n按回车键退出..." << std::endl;
    std::cin.get();
}
static std::string getDemoLRC() {
    return R"([00:00.00]已连接，切歌后显示正常(From:VacuumMusicPlayer))";
}
void AMLLWebSocketClient::sendAlbumCoverByURI(const std::string& dataUri) {
    auto buf = serializeSetAlbumCoverURI(dataUri);
    webSocket_.sendBinary(buf);
    std::cout << "[AMLL] 发送封面 URI" << std::endl;
    std::string hex;
    for (size_t i = 0; i < (std::min)(buf.size(), (size_t)64); ++i) {
        char tmp[4];
        sprintf_s(tmp, "%02X ", buf[i]);
        hex += tmp;
    }
    OutputDebugStringA(("images-----bytes_go---(" + std::to_string(buf.size()) + " bytes): " + hex + "\n").c_str());
}
void AMLLWebSocketClient::sendInitialData() {
    // 基本音乐信息
    std::vector<Artist> artists = { {"artist_0", "Unknow Artist"} };
    sendMusicInfo("song_0", "Unknow Data", "album_0", "Unknow Album", artists, 100);

    // 专辑封面（若存在 cover.png）

    // 歌词（使用内置 LRC 示例，会转换为 TTML 后发送）
    sendLyricFromLRC(getDemoLRC());
    std::cout << "-------------------------" << std::endl;
}
void AMLLWebSocketClient::sendMusicInfo(const std::string& musicId,
    const std::string& musicName,
    const std::string& albumId,
    const std::string& albumName,
    const std::vector<Artist>& artists,
    uint64_t durationMs) {
    auto buf = serializeSetMusicInfo(musicId, musicName, albumId, albumName, artists, durationMs);
    webSocket_.sendBinary(buf);
    OutputDebugStringA("[AMLL] sendMusicInfo RUNNING---------\n");
    OutputDebugStringA(("connected_=" + std::to_string(connected_) + "\n").c_str());
    if (webSocket_.getReadyState() != ix::ReadyState::Open) {
        OutputDebugStringA("WebSocket not open, message dropped\n");
        return;
    }
    std::string hex;
    for (size_t i = 0; i < (std::min)(buf.size(), (size_t)64); ++i) {
        char tmp[4];
        sprintf_s(tmp, "%02X ", buf[i]);
        hex += tmp;
    }
    OutputDebugStringA(("bytes_go---(" + std::to_string(buf.size()) + " bytes): " + hex + "\n").c_str());
    std::cout << "[AMLL] 发送音乐信息" << std::endl;
}

void AMLLWebSocketClient::sendAlbumCover(const std::vector<uint8_t>& imageData) {
    if (imageData.empty()) {
        std::cout << "[AMLL] 封面数据为空，跳过发送" << std::endl;
        return;
    }
    auto buf = serializeSetAlbumCoverData(imageData);
    webSocket_.sendBinary(buf);
    std::cout << "[AMLL] 发送专辑封面，大小: " << imageData.size() << " 字节" << std::endl;
}

void AMLLWebSocketClient::sendLyricFromLRC(const std::string& lrcContent) {
    std::string ttml = convertLRCToTTML(lrcContent);
    if (ttml.empty()) {
        std::cout << "[AMLL] LRC 转换失败" << std::endl;
        return;
    }
    auto buf = serializeSetLyricFromTTML(ttml);
    webSocket_.sendBinary(buf);
    std::cout << "[AMLL] 发送 TTML 歌词" << std::endl;
}

void AMLLWebSocketClient::sendResumed() {
    auto buf = serializeOnResumed();
    webSocket_.sendBinary(buf);
    std::cout << "[AMLL] 发送恢复播放" << std::endl;
}

void AMLLWebSocketClient::sendPaused() {
    auto buf = serializeOnPaused();
    webSocket_.sendBinary(buf);
    std::cout << "[AMLL] 发送暂停" << std::endl;
}

void AMLLWebSocketClient::onMessage(const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
        std::cout << "[AMLL] WebSocket 连接成功" << std::endl;
        connected_ = true;
        OutputDebugStringA("[AMLL]connected--------------\n");
        sendInitialData();
    }
    else if (msg->type == ix::WebSocketMessageType::Message) {
        // 简单解析消息类型并打印
        const uint8_t* p = (const uint8_t*)msg->str.data();
        if (msg->str.size() >= 2) {
            MsgType type = (MsgType)read_u16(p);
            std::cout << "[AMLL] 收到消息，类型: " << (uint16_t)type << std::endl;
            OutputDebugStringA("[AMLL]connected-message----------------\n");
            switch (type) {
            case MsgType::Pause:           cmdQueue_.push(AMLLCommand::Pause); break;
            case MsgType::Resume:          cmdQueue_.push(AMLLCommand::Resume); break;
            case MsgType::ForwardSong:     cmdQueue_.push(AMLLCommand::Forward); break;
            case MsgType::BackwardSong:    cmdQueue_.push(AMLLCommand::Backward); break;
            case MsgType::SeekPlayProgress:
                if (msg->str.size() >= 2 + sizeof(uint64_t)) {
                    cmdQueue_.push(AMLLCommand::Seek);
                    PendingSeek = read_u64(p);
                }
                break;
            }
            
        }
    }
    else if (msg->type == ix::WebSocketMessageType::Error) {
        std::cout << "[AMLL] 错误: " << msg->errorInfo.reason << std::endl;
    }
    else if (msg->type == ix::WebSocketMessageType::Close) {
        std::cout << "[AMLL] 连接关闭" << std::endl;
        connected_ = false;
    }
    else if (msg->type == ix::WebSocketMessageType::Ping) {
        webSocket_.sendBinary(serializePong());
        std::cout << "[AMLL] 响应 Pong" << std::endl;
        OutputDebugStringA("[AMLL]-----Pong------------------\n");
    }
}
AMLLCommand AMLLWebSocketClient::getNextCommand() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    if (cmdQueue_.empty()) return AMLLCommand::None;
    AMLLCommand cmd = cmdQueue_.front();
    cmdQueue_.pop();
    return cmd;
}
void AMLLWebSocketClient::sendProgress(uint64_t pos) {
    if (connected_) {
        auto buf = serializeOnPlayProgress(pos);
        webSocket_.sendBinary(buf);
    }
}