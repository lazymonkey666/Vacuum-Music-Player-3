#include "AMLLWebSocketV2.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <iomanip>
#include <chrono>
#include <thread>
#include <ixwebsocket/IXNetSystem.h>
#include <algorithm>

extern HWND g_hWnd;

// ========== 辅助：XML 转义（内部） ==========
namespace {
    std::string xml_escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char ch : s) {
            switch (ch) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;";  break;
            case '>':  out += "&gt;";  break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += ch;
            }
        }
        return out;
    }
}
namespace {
    // 判断是否为中文字符（基本汉字及扩展）
    bool isChineseCodepoint(uint32_t cp) {
        return (cp >= 0x4E00 && cp <= 0x9FFF) ||
            (cp >= 0x3400 && cp <= 0x4DBF) ||
            (cp >= 0x20000 && cp <= 0x2A6DF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) ||
            (cp >= 0x2F800 && cp <= 0x2FA1F);
    }

    // 获取下一个 UTF-8 码点，并移动指针
    uint32_t utf8_next(const char*& p) {
        uint8_t c = *p;
        if (c < 0x80) { p++; return c; }
        int len = 0;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else { p++; return 0; } // 非法
        uint32_t cp = 0;
        for (int i = 0; i < len; ++i) {
            cp = (cp << 6) | (*(p + i) & 0x3F);
        }
        p += len;
        return cp;
    }

    // 统计字符串中的中英文（字母）个数
    std::pair<int, int> countLanguages(const std::string& s) {
        int zh = 0, en = 0;
        const char* p = s.c_str();
        while (*p) {
            uint32_t cp = utf8_next(p);
            if (isChineseCodepoint(cp)) {
                zh++;
            }
            else if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
                en++;
            }
        }
        return { zh, en };
    }

    // 判断字符串是否包含中文字符
    bool containsChinese(const std::string& s) {
        const char* p = s.c_str();
        while (*p) {
            uint32_t cp = utf8_next(p);
            if (isChineseCodepoint(cp)) return true;
        }
        return false;
    }

    // 判断字符串是否包含英文字母
    bool containsEnglish(const std::string& s) {
        const char* p = s.c_str();
        while (*p) {
            uint32_t cp = utf8_next(p);
            if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
        }
        return false;
    }

    // 歌词翻译分割（参照 Python 算法）
    std::pair<std::string, std::string> splitTranslation(const std::string& lineText, const std::string& mainLang) {
        // 按空格分割 tokens
        std::vector<std::string> tokens;
        std::istringstream iss(lineText);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        if (tokens.empty()) return { lineText, "" };

        std::string orig, trans;
        size_t half = tokens.size() / 2;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& tok = tokens[i];
            bool isChinese = containsChinese(tok);
            bool isEnglish = containsEnglish(tok);
            bool isTranslation = false;
            if (mainLang == "en") {
                // 若为中文且位置在后半，视为翻译
                if (isChinese && i >= half) isTranslation = true;
            }
            else if (mainLang == "zh") {
                // 若为英文且位置在后半，视为翻译
                if (isEnglish && i >= half) isTranslation = true;
            }
            if (isTranslation) {
                if (!trans.empty()) trans += " ";
                trans += tok;
            }
            else {
                if (!orig.empty()) orig += " ";
                orig += tok;
            }
        }
        return { orig, trans };
    }
    bool isMetadataLine(const std::string& text) {
        // 去除首尾空格
        std::string trimmed = text;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        if (trimmed.empty()) return false;

        // 检测是否包含冒号（中英文），且整行较短，且冒号前是制作关键词
        size_t colonPos = trimmed.find(':');
        if (colonPos == std::string::npos) colonPos = trimmed.find('：');
        if (colonPos != std::string::npos && trimmed.length() < 60) {
            // 冒号前的部分
            std::string prefix = trimmed.substr(0, colonPos);
            // 去除空格
            prefix.erase(0, prefix.find_first_not_of(" \t"));
            prefix.erase(prefix.find_last_not_of(" \t") + 1);
            // 关键词列表（中英文）
            static const std::vector<std::string> keywords = {
                "作词", "作曲", "编曲", "制作人", "制作", "录音", "混音", "母带",
                "吉他", "贝斯", "鼓", "钢琴", "打击乐", "和声", "弦乐", "管乐",
                "监制", "策划", "统筹", "文案", "设计", "摄影", "MV", "导演",
                "主演", "友情出演", "特别感谢", "OP", "SP", "版权", "出品", "发行",
                "电吉他", "Organ", "口琴", "录音室", "混音室", "母带工程室",
                "录音助理", "封面设计"
            };
            for (const auto& kw : keywords) {
                if (prefix.find(kw) != std::string::npos) {
                    return true;
                }
            }
            // 特殊英文名称
            if (prefix.find("Sterling") != std::string::npos ||
                prefix.find("Randy") != std::string::npos ||
                prefix.find("TEC") != std::string::npos ||
                prefix.find("Merrill") != std::string::npos ||
                prefix.find("Sound") != std::string::npos) {
                return true;
            }
        }
        // 若整行长度很短（<30）且包含英文冒号，也可能是制作信息
        if (trimmed.length() < 30 && trimmed.find(':') != std::string::npos) {
            return true;
        }
        return false;
    }
}

// ========== base64 编码（与 V1 兼容） ==========
std::string AMLLWebSocketClient::base64_encode(const std::vector<uint8_t>& data) {
    const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i < data.size(); i += 3) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1 < data.size()) ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2 < data.size()) ? data[i + 2] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result.push_back(base64_chars[(triple >> 18) & 0x3F]);
        result.push_back(base64_chars[(triple >> 12) & 0x3F]);
        result.push_back(base64_chars[(triple >> 6) & 0x3F]);
        result.push_back(base64_chars[triple & 0x3F]);
    }

    size_t mod = data.size() % 3;
    if (mod == 1) {
        result[result.size() - 1] = '=';
        result[result.size() - 2] = '=';
    }
    else if (mod == 2) {
        result[result.size() - 1] = '=';
    }
    return result;
}

// ========== V1 兼容的 imageToDataURI ==========
std::string AMLLWebSocketClient::imageToDataURI(const std::vector<uint8_t>& imageData, const std::string& mimeType) {
    return "data:" + mimeType + ";base64," + base64_encode(imageData);
}

// ========== LRC → TTML 转换（与 V1 一致） ==========
double AMLLWebSocketClient::parseLrcTime(const std::string& tag) {
    std::regex re(R"(\[(\d{2}):(\d{2})(?:\.(\d{2}))?\])");
    std::smatch m;
    if (std::regex_search(tag, m, re)) {
        int minutes = std::stoi(m[1]);
        int seconds = std::stoi(m[2]);
        int centiseconds = (m.size() > 3 && m[3].matched) ? std::stoi(m[3]) : 0;
        return minutes * 60.0 + seconds + centiseconds / 100.0;
    }
    return 0.0;
}

std::vector<LyricLine> AMLLWebSocketClient::convertLRCToStructured(const std::string& lrc,
    uint64_t defaultDurationMs,
    bool enableSplit) {
    struct RawItem { uint64_t time; std::string text; bool isMetadata; };
    std::vector<RawItem> rawItems;
    std::regex pattern(R"(\[(\d{2}):(\d{2})\.(\d{2,3})\](.*))");
    std::istringstream iss(lrc);
    std::string line;
    while (std::getline(iss, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, pattern)) continue;
        int minute = std::stoi(match[1]);
        int sec = std::stoi(match[2]);
        std::string msStr = match[3].str();
        while (msStr.length() < 3) msStr += '0';
        int ms = std::stoi(msStr);
        uint64_t timeMs = minute * 60 * 1000 + sec * 1000 + ms;
        std::string text = match[4];
        text.erase(0, text.find_first_not_of(" \t\n\r"));
        text.erase(text.find_last_not_of(" \t\n\r") + 1);
        if (text.empty()) continue;

        bool isMeta = isMetadataLine(text);
        rawItems.push_back({ timeMs, text, isMeta });
    }
    if (rawItems.empty()) return {};

    // 统计语言分布（仅统计非元数据行）
    int totalZh = 0, totalEn = 0;
    for (const auto& item : rawItems) {
        if (!item.isMetadata) {
            auto [zh, en] = countLanguages(item.text);
            totalZh += zh; totalEn += en;
        }
    }

    // 决定是否启用分割：仅当原曲为英文且翻译为中文时
    std::string origLang, transLang;
    if (enableSplit && totalEn > totalZh * 0.2) { // 英文占主导，且足够多
        origLang = "en";
        transLang = "zh"; // 只支持英文→中文
    }
    else {
        origLang = "";   // 不分割
        transLang = "";
    }
    if (!enableSplit) transLang = "";

    // 构建结构化歌词
    std::vector<LyricLine> result;
    for (size_t i = 0; i < rawItems.size(); ++i) {
        uint64_t start = rawItems[i].time;
        uint64_t end = (i + 1 < rawItems.size()) ? rawItems[i + 1].time : (start + defaultDurationMs);

        LyricLine line;
        line.start_time = start;
        line.end_time = end;

        std::string origText, transText;
        if (rawItems[i].isMetadata || transLang.empty()) {
            origText = rawItems[i].text;
            transText = "";
        }
        else {
            auto [orig, trans] = splitTranslation(rawItems[i].text, origLang);
            origText = orig;
            transText = trans;
        }

        LyricWord word;
        word.start_time = start;
        word.end_time = end;
        word.word = origText;
        line.words.push_back(word);
        line.translated_lyric = transText;
        line.roman_lyric = "";
        line.flag = 0;
        result.push_back(line);
    }
    return result;
}

// ========== 构造函数 / 析构函数 ==========
AMLLWebSocketClient::AMLLWebSocketClient(const std::string& url)
    : url_(url), connected_(false), running_(true) {
    ix::initNetSystem();
    webSocket_.setUrl(url_);
    webSocket_.setPingInterval(45);
    webSocket_.enableAutomaticReconnection();
    webSocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        onMessage(msg);
        });
    PendingSeek.store(UINT64_MAX);
}

AMLLWebSocketClient::~AMLLWebSocketClient() {
    stop();
    ix::uninitNetSystem();
}

// ========== start() 中不再调用 sendInitialData ==========
void AMLLWebSocketClient::start() {
    webSocket_.start();   // 仅启动连接，数据发送在 onMessage 的 Open 中完成
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

// ========== 内部 JSON 发送辅助 ==========
void AMLLWebSocketClient::sendJson(const json& msg) {
    if (webSocket_.getReadyState() != ix::ReadyState::Open) {
        std::cout << "[AMLL] WebSocket未打开，丢弃消息" << std::endl;
        return;
    }
    std::string str = msg.dump();
    webSocket_.sendText(str);
}

json AMLLWebSocketClient::makeStateMsg(json valueObj) {
    json msg;
    msg["type"] = "state";
    msg["value"] = valueObj;
    return msg;
}

// ========== V1 兼容公共接口实现（使用 V2 JSON 协议） ==========
void AMLLWebSocketClient::sendMusicInfo(const std::string& musicId,
    const std::string& musicName,
    const std::string& albumId,
    const std::string& albumName,
    const std::vector<Artist>& artists,
    uint64_t durationMs) {
    json value;
    value["update"] = "setMusic";
    value["musicId"] = musicId;
    value["musicName"] = musicName;
    value["albumId"] = albumId;
    value["albumName"] = albumName;
    json artistArray = json::array();
    for (const auto& a : artists) {
        artistArray.push_back({ {"id", a.id}, {"name", a.name} });
    }
    value["artists"] = artistArray;
    value["duration"] = durationMs;
    sendJson(makeStateMsg(value));
    std::cout << "[AMLL] 发送音乐信息 (V2 JSON)" << std::endl;
}

void AMLLWebSocketClient::sendAlbumCover(const std::vector<uint8_t>& imageData) {
    if (imageData.empty()) {
        std::cout << "[AMLL] 封面数据为空，跳过发送" << std::endl;
        return;
    }
    json value;
    value["update"] = "setCover";
    value["source"] = "data";
    value["image"]["data"] = base64_encode(imageData);
    value["image"]["mimeType"] = "image/png";
    sendJson(makeStateMsg(value));
    std::cout << "[AMLL] 发送专辑封面 (V2 JSON)，大小: " << imageData.size() << " 字节" << std::endl;
}

void AMLLWebSocketClient::sendLyricFromLRC(const std::string& lrcContent, bool enableSplit) {
    auto lines = convertLRCToStructured(lrcContent, 5000, enableSplit);
    if (lines.empty()) {
        std::cout << "[AMLL] LRC 转换失败或无歌词" << std::endl;
        return;
    }

    json value;
    value["update"] = "setLyric";
    value["format"] = "structured";

    json linesArray = json::array();
    for (const auto& line : lines) {
        json lineObj;
        lineObj["startTime"] = line.start_time;
        lineObj["endTime"] = line.end_time;
        json wordsArray = json::array();
        for (const auto& word : line.words) {
            json wordObj;
            wordObj["startTime"] = word.start_time;
            wordObj["endTime"] = word.end_time;
            wordObj["word"] = word.word;
            wordsArray.push_back(wordObj);
        }
        lineObj["words"] = wordsArray;
        if (!line.translated_lyric.empty()) {
            lineObj["translatedLyric"] = line.translated_lyric;
        }
        linesArray.push_back(lineObj);
    }
    value["lines"] = linesArray;

    sendJson(makeStateMsg(value));
    std::cout << "[AMLL] 发送结构化歌词 (V2 JSON)，共 " << lines.size() << " 行";
    if (enableSplit) {
        std::cout << "，已启用翻译分割";
    }
    else {
        std::cout << "，未启用翻译分割";
    }
    std::cout << std::endl;
}

void AMLLWebSocketClient::sendResumed() {
    json value;
    value["update"] = "resumed";
    sendJson(makeStateMsg(value));
    std::cout << "[AMLL] 发送恢复播放 (V2 JSON)" << std::endl;
}

void AMLLWebSocketClient::sendPaused() {
    json value;
    value["update"] = "paused";
    sendJson(makeStateMsg(value));
    std::cout << "[AMLL] 发送暂停 (V2 JSON)" << std::endl;
}

void AMLLWebSocketClient::sendProgress(uint64_t posMs) {
    json value;
    value["update"] = "progress";
    value["progress"] = posMs;
    sendJson(makeStateMsg(value));
}

void AMLLWebSocketClient::sendAlbumCoverByURI(const std::string& dataUri) {
    json value;
    value["update"] = "setCover";
    value["source"] = "uri";
    value["url"] = dataUri;
    sendJson(makeStateMsg(value));
    std::cout << "[AMLL] 发送封面 URI (V2 JSON)" << std::endl;
}

uint64_t AMLLWebSocketClient::getPendingSeek() {
    return PendingSeek.exchange(UINT64_MAX);
}

// ========== V2 新增功能 ==========
void AMLLWebSocketClient::sendLyricTTML(const std::string& ttmlXml) {
    json value;
    value["update"] = "setLyric";
    value["format"] = "ttml";
    value["data"] = ttmlXml;
    sendJson(makeStateMsg(value));
}

void AMLLWebSocketClient::sendVolume(float vol) {
    json value;
    value["update"] = "volume";
    value["volume"] = vol;
    sendJson(makeStateMsg(value));
}

void AMLLWebSocketClient::sendAudioData(const std::vector<uint8_t>& pcmData) {
    if (webSocket_.getReadyState() != ix::ReadyState::Open) return;
    std::vector<uint8_t> buf;
    uint16_t magic = 0;
    uint32_t size = static_cast<uint32_t>(pcmData.size());
    buf.insert(buf.end(), (const uint8_t*)&magic, (const uint8_t*)&magic + 2);
    buf.insert(buf.end(), (const uint8_t*)&size, (const uint8_t*)&size + 4);
    buf.insert(buf.end(), pcmData.begin(), pcmData.end());
    webSocket_.sendBinary(buf);
}

// ========== 唯一的 sendInitialData（包含 initialize） ==========
static std::string getDemoLRC() {
    return R"([00:00.00]已连接，切歌后显示正常 (From:VacuumMusicPlayer))";
}

void AMLLWebSocketClient::sendInitialData() {
    // 1. 发送初始化消息（V2 协议必须）
    json init;
    init["type"] = "initialize";
    sendJson(init);
    std::cout << "[AMLL] 发送初始化消息 (V2)" << std::endl;

    // 2. 发送示例数据
    std::vector<Artist> artists = { {"artist_0", "Unknown Artist"} };
    sendMusicInfo("song_0", "Unknown Data", "album_0", "Unknown Album", artists, 100);
    sendLyricFromLRC(getDemoLRC(),false);
    std::cout << "-------------------------" << std::endl;
}

// ========== 消息回调 ==========
void AMLLWebSocketClient::onMessage(const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
        std::cout << "[AMLL] WebSocket 连接成功" << std::endl;
        connected_ = true;
        sendInitialData();   // 仅在连接成功后发送初始化
    }
    else if (msg->type == ix::WebSocketMessageType::Message) {
        if (msg->binary) {
            std::cout << "[AMLL] 收到二进制消息，忽略" << std::endl;
            return;
        }
        try {
            json j = json::parse(msg->str);
            if (!j.contains("type")) return;
            std::string type = j["type"];

            if (type == "command") {
                json value = j["value"];
                std::string command = value["command"];
                if (command == "pause") {
                    std::lock_guard<std::mutex> lock(cmdMutex_);
                    cmdQueue_.push(AMLLCommand::Pause);
                }
                else if (command == "resume") {
                    std::lock_guard<std::mutex> lock(cmdMutex_);
                    cmdQueue_.push(AMLLCommand::Resume);
                }
                else if (command == "forwardSong") {
                    std::lock_guard<std::mutex> lock(cmdMutex_);
                    cmdQueue_.push(AMLLCommand::Forward);
                }
                else if (command == "backwardSong") {
                    std::lock_guard<std::mutex> lock(cmdMutex_);
                    cmdQueue_.push(AMLLCommand::Backward);
                }
                else if (command == "seekPlayProgress") {
                    if (value.contains("progress")) {
                        uint64_t progress = value["progress"];
                        std::lock_guard<std::mutex> lock(cmdMutex_);
                        cmdQueue_.push(AMLLCommand::Seek);
                        PendingSeek.store(progress);
                    }
                }
                // 可扩展其他命令
            }
            else if (type == "ping") {
                json pong;
                pong["type"] = "pong";
                sendJson(pong);
                std::cout << "[AMLL] 响应 Pong" << std::endl;
            }
            else if (type == "initialize") {
                // 忽略
            }
        }
        catch (const std::exception& e) {
            std::cout << "[AMLL] JSON 解析错误: " << e.what() << std::endl;
        }
    }
    else if (msg->type == ix::WebSocketMessageType::Error) {
        std::cout << "[AMLL] 错误: " << msg->errorInfo.reason << std::endl;
    }
    else if (msg->type == ix::WebSocketMessageType::Close) {
        std::cout << "[AMLL] 连接关闭" << std::endl;
        connected_ = false;
    }
}

AMLLCommand AMLLWebSocketClient::getNextCommand() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    if (cmdQueue_.empty()) return AMLLCommand::None;
    AMLLCommand cmd = cmdQueue_.front();
    cmdQueue_.pop();
    return cmd;
}