#include <utils.h>


// ═══════════════════════════════════════════════════════════
// Base64 通用实现（跨平台）
// ═══════════════════════════════════════════════════════════
static const char kBase64Chars[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();

    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = bytes[i] << 16;
        if (i + 1 < len) n |= bytes[i + 1] << 8;
        if (i + 2 < len) n |= bytes[i + 2];

        out.push_back(kBase64Chars[(n >> 18) & 0x3F]);
        out.push_back(kBase64Chars[(n >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? kBase64Chars[(n >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? kBase64Chars[n & 0x3F] : '=');
    }
    return out;
}

std::string Base64Decode(const std::string& input) {
    static int decodeTable[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (int i = 0; i < 256; ++i) decodeTable[i] = -1;
        for (int i = 0; i < 64; ++i) decodeTable[(unsigned char)kBase64Chars[i]] = i;
        tableReady = true;
    }

    std::string out;
    out.reserve(input.size() * 3 / 4);

    int buf = 0, bits = 0;
    for (unsigned char c : input) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = decodeTable[c];
        if (v < 0) continue;  // 跳过非法字符
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

namespace fs = std::filesystem;

// 解析 LRC 内容，返回按时间排序的 (毫秒, 文本) 列表
std::vector<std::pair<int, std::string>> ParseLrc(const std::string& lrcContent) {
    std::vector<std::pair<int, std::string>> lines;
    std::regex pattern(R"(\[(\d+):(\d+(?:\.\d+)?)\](.*))");
    std::smatch match;
    std::istringstream stream(lrcContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (std::regex_search(line, match, pattern)) {
            int minutes = std::stoi(match[1].str());
            double seconds = std::stod(match[2].str());
            int totalMs = (int)(minutes * 60000 + seconds * 1000);
            std::string text = match[3].str();
            if (!text.empty()) {
                lines.emplace_back(totalMs, text);
            }
        }
    }
    // 按时间排序
    std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
        });
    return lines;
}

uint64_t GetAudioFileDurationMs(const std::string& utf8Path) {
    PathType path = Utf8ToPath(utf8Path);
    if (path.empty()) return 0;

    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0);
    if (MA_DECODER_INIT_FILE(path.c_str(), &config, &decoder) != MA_SUCCESS)
        return 0;

    ma_uint64 totalFrames = 0;
    ma_result res = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    double seconds = 0.0;
    if (res == MA_SUCCESS) {
        seconds = (double)totalFrames / decoder.outputSampleRate;
    }
    ma_decoder_uninit(&decoder);

    // ★ 这里返回 uint64_t
    return (uint64_t)(seconds * 1000.0);
}

std::string FormatTime(uint64_t milliseconds) {
    int totalSeconds = (int)(milliseconds / 1000);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    return std::string(buf);
}

std::string ExtractExtensionFromUrl(const std::string& url) {
    size_t queryPos = url.find('?');
    std::string path = (queryPos == std::string::npos) ? url : url.substr(0, queryPos);
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos && dotPos + 1 < path.size()) {
        std::string ext = path.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".mp3" || ext == ".flac") {
            return ext;
        }
    }
    return "";
}

std::string U8StringToString(const std::u8string& u8str) {
    return std::string(reinterpret_cast<const char*>(u8str.data()), u8str.size());
}

MusicMetadata GetMusicMetadata(const std::string& filepath_utf8) {
    MusicMetadata meta;

    PathType path = Utf8ToPath(filepath_utf8);
    if (path.empty()) {
        size_t pos = filepath_utf8.find_last_of("/\\");
        meta.title = (pos == std::string::npos) ? filepath_utf8 : filepath_utf8.substr(pos + 1);
        return meta;
    }

    TagLib::FileRef f(path.c_str());
    if (!f.isNull() && f.tag()) {
        TagLib::Tag* tag = f.tag();
        meta.title = tag->title().to8Bit(true);
        meta.artist = tag->artist().to8Bit(true);
        meta.album = tag->album().to8Bit(true);
    }
    else {
        size_t pos = filepath_utf8.find_last_of("/\\");
        meta.title = (pos == std::string::npos) ? filepath_utf8 : filepath_utf8.substr(pos + 1);
        OutputDebugStringA("[GetMusicMetaData]没有读取到信息\n");
    }
    return meta;
}

PathType GetCacheDirectory() {
    // GetTempDir 现在返回 UTF-8
    std::string tempUtf8 = GetTempDir();
    std::string cacheUtf8 = tempUtf8 + "Vacuum Music Player/MusicCache/";

    PathType cacheW = Utf8ToPath(cacheUtf8);
    if (cacheW.empty()) return {};

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(cacheW), ec);
    return cacheW;   // ✅ 宽字符
}