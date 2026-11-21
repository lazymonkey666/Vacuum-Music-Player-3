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

std::vector<std::string> split(const std::string& word, const std::string& s) {
    if (s.empty()) {
        throw std::invalid_argument("split(): empty separator");
    }

    std::vector<std::string> result;
    size_t start = 0;
    size_t pos = 0;

    while ((pos = word.find(s, start)) != std::string::npos) {
        // 提取从 start 到 pos 之间的子串（可能为空）
        result.push_back(word.substr(start, pos - start));
        start = pos + s.length();
    }

    // 添加最后一个片段（可能为空）
    result.push_back(word.substr(start));

    return result;
}


// ═══════════════════════════════════════════════════════════════════
// 写入音频元数据（标签 + 封面 + 歌词）
// ═══════════════════════════════════════════════════════════════════
void WriteMetadataToFile(const std::string& filePath,
    const std::string& title,
    const std::vector<std::string>& artist,
    const std::string& album,
    const std::string& coverPath,
    const std::string& lyricsText)
{
    // ── 1. 路径 → 原生类型（Windows: wstring，Linux: 原样 UTF-8 string）──
    PathType fileW = Utf8ToPath(filePath);
    if (fileW.empty()) {
        OutputDebugStringA("[WriteMetadataToFile] filePath 转换失败\n");
        return;
    }

    // ── 2. 扩展名：u8string() 输出 → 纯 ASCII 比较，不喂回 fs::path ──
    std::string ext = U8StringToString(
        std::filesystem::path(fileW).extension().u8string());
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    // ── 3. 艺术家拼接（多艺术家用 " & " 连接）──
    std::string artistResult;
    for (size_t i = 0; i < artist.size(); ++i) {
        if (i > 0) artistResult += " & ";
        artistResult += artist[i];
    }
    // 恢复flac封面写入
    // ── 4. 预读封面（如果存在），避免下游两分支重复代码 ──
    TagLib::ByteVector imgData;
    std::string coverExt;
    if (!coverPath.empty()) {
        PathType coverW = Utf8ToPath(coverPath);
        std::error_code ec;
        if (!coverW.empty()
            && std::filesystem::exists(coverW, ec) && !ec) {

            std::ifstream imgFs(std::filesystem::path(coverW), std::ios::binary);
            if (imgFs.is_open()) {
                imgFs.seekg(0, std::ios::end);
                const std::streamoff imgSize = imgFs.tellg();
                if (imgSize > 0) {
                    imgFs.seekg(0);
                    std::vector<char> buf(static_cast<size_t>(imgSize));
                    imgFs.read(buf.data(), imgSize);
                    imgData = TagLib::ByteVector(buf.data(), buf.size());

                    coverExt = U8StringToString(
                        std::filesystem::path(coverW).extension().u8string());
                    std::transform(coverExt.begin(), coverExt.end(), coverExt.begin(),
                        [](unsigned char c) { return (char)std::tolower(c); });
                }
                imgFs.close();
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // FLAC 分支
    // ═══════════════════════════════════════════════════════════════
    if (ext == ".flac") {
        TagLib::FLAC::File file(fileW.c_str());
        if (!file.isValid()) {
            OutputDebugStringA("[WriteMetadataToFile] FLAC 文件无效\n");
            return;
        }

        // true = xiphComment 不存在时自动创建
        auto* tag = file.xiphComment(true);
        if (!tag) {
            OutputDebugStringA("[WriteMetadataToFile] xiphComment 获取失败\n");
            return;
        }

        // 只清空文本字段，封面单独处理
        tag->removeAllFields();

        tag->addField("TITLE", TagLib::String(title, TagLib::String::UTF8), true);
        tag->addField("ARTIST", TagLib::String(artistResult, TagLib::String::UTF8), true);
        tag->addField("ALBUM", TagLib::String(album, TagLib::String::UTF8), true);
        if (!lyricsText.empty()) {
            tag->addField("LYRICS", TagLib::String(lyricsText, TagLib::String::UTF8), true);
        }

        // 封面
        if (!imgData.isEmpty()) {
            file.removePictures();   // 清除已有全部封面

            auto* pic = new TagLib::FLAC::Picture();
            pic->setMimeType(coverExt == ".png" ? "image/png" : "image/jpeg");
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setDescription("Album Cover");
            pic->setData(imgData);
            file.addPicture(pic);
        }

        if (!file.save()) {
            OutputDebugStringA("[WriteMetadataToFile] FLAC 保存失败\n");
        }
        return;
    }

    // ═══════════════════════════════════════════════════════════════
    // MP3 分支（其他扩展名默认走这里）
    // ═══════════════════════════════════════════════════════════════
    TagLib::MPEG::File mpegFile(fileW.c_str());
    if (!mpegFile.isValid()) {
        OutputDebugStringA("[WriteMetadataToFile] MP3 文件无效\n");
        return;
    }

    // 通用基础标签（兼容 ID3v1 / ID3v2 通用层）
    TagLib::Tag* baseTag = mpegFile.tag();
    if (baseTag) {
        baseTag->setTitle(TagLib::String(title, TagLib::String::UTF8));
        baseTag->setArtist(TagLib::String(artistResult, TagLib::String::UTF8));
        baseTag->setAlbum(TagLib::String(album, TagLib::String::UTF8));
    }

    // ID3v2 详细标签
    TagLib::ID3v2::Tag* id3Tag = mpegFile.ID3v2Tag(true);
    if (!id3Tag) {
        OutputDebugStringA("[WriteMetadataToFile] ID3v2 Tag 创建失败\n");
        return;
    }

    // 封面（APIC）
    if (!imgData.isEmpty()) {
        auto apicFrames = id3Tag->frameList("APIC");
        for (auto* frame : apicFrames) {
            id3Tag->removeFrame(frame);
        }

        auto* picFrame = new TagLib::ID3v2::AttachedPictureFrame();
        picFrame->setPicture(imgData);
        picFrame->setMimeType(coverExt == ".png" ? "image/png" : "image/jpeg");
        picFrame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
        picFrame->setDescription("Album Cover");
        id3Tag->addFrame(picFrame);
    }

    // 歌词（USLT）
    if (!lyricsText.empty()) {
        auto usltFrames = id3Tag->frameList("USLT");
        for (auto* frame : usltFrames) {
            id3Tag->removeFrame(frame);
        }

        auto* lyricsFrame = new TagLib::ID3v2::UnsynchronizedLyricsFrame();
        lyricsFrame->setLanguage("chi");
        lyricsFrame->setDescription("");
        lyricsFrame->setText(TagLib::String(lyricsText, TagLib::String::UTF8));
        id3Tag->addFrame(lyricsFrame);
    }

    if (!mpegFile.save(TagLib::MPEG::File::AllTags)) {
        OutputDebugStringA("[WriteMetadataToFile] MP3 保存失败\n");
    }
}

//缓存相关
uint64_t GetCacheSize(const PathType& cacheDir) {
    uint64_t totalSize = 0;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(cacheDir)) {
            if (entry.is_regular_file()) totalSize += entry.file_size();
        }
    }
    catch (...) {}
    return totalSize;
}

void TrimCache(const PathType& cacheDir, uint64_t limitBytes) {
    using FileInfo = std::pair<std::filesystem::file_time_type, std::filesystem::path>;
    std::priority_queue<FileInfo, std::vector<FileInfo>, std::greater<FileInfo>> oldestFirst;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
            if (entry.is_regular_file())
                oldestFirst.emplace(entry.last_write_time(), entry.path());
        }
    }
    catch (...) { return; }

    uint64_t currentSize = GetCacheSize(cacheDir);
    while (currentSize > limitBytes && !oldestFirst.empty()) {
        auto [time, path] = oldestFirst.top();
        oldestFirst.pop();
        try {
            uint64_t fileSize = std::filesystem::file_size(path);
            std::filesystem::remove(path);
            currentSize -= fileSize;
        }
        catch (...) {}
    }
}

// 清理缓存（若大小超过上限）
void EnforceCacheLimit(uint64_t limitMB) {
    PathType cacheDir = GetCacheDirectory();
    if (cacheDir.empty()) return;
    uint64_t limitBytes = limitMB * 1024 * 1024;
    TrimCache(cacheDir, limitBytes);
}


// 返回空字符串代表失败，成功返回真实的本地绝对路径
std::string DownloadAndProcessTrack(const PlaybackInfo& info) {
    if (!info.isOnline) return info.audioUrl;

    std::string extension = (info.audioUrl.find(".flac") != std::string::npos) ? ".flac" : ".mp3";

    PathType cacheW = GetCacheDirectory();
    if (cacheW.empty()) return "";

    PathType localW = cacheW + Utf8ToPath(info.localCacheId + extension);
    PathType imageW = cacheW + Utf8ToPath("temp_cover_" + info.localCacheId + ".jpg");

    std::error_code ec;
    if (std::filesystem::exists(localW, ec) && !ec
        && std::filesystem::file_size(localW, ec) > 0 && !ec) {
        std::string localUtf8 = PathToUtf8(localW);
        OutputDebugStringA(("使用缓存: " + localUtf8 + "\n").c_str());
        return localUtf8;
    }

    // 下载函数接收 UTF-8（内部已改为 URLDownloadToFileW）
    std::string localUtf8 = PathToUtf8(localW);
    std::string imageUtf8 = PathToUtf8(imageW);

    SetStatus(STATUS_DOWNLOADING);
    if (!DownloadFile(info.audioUrl, localUtf8)) {
        OutputDebugStringA(("下载音频失败: " + info.audioUrl + "\n").c_str());
        return "";
    }

    if (info.needTags) {
        SetStatus(STATUS_FETCHING_META);
        if (!info.albumArtUrl.empty()) DownloadFile(info.albumArtUrl, imageUtf8);

        SetStatus(STATUS_WRITING_TAGS);

        std::vector<std::string> _artists = { info.artist };

        std::error_code ec2;
        if (std::filesystem::exists(imageW, ec2) && !ec2) {
            WriteMetadataToFile(localUtf8, info.title, _artists, info.album, imageUtf8, info.lyrics);
            std::filesystem::remove(imageW, ec2);
        }
        else {
            WriteMetadataToFile(localUtf8, info.title, _artists, info.album, "", info.lyrics);
        }
    }

    if (g_cache_clean) EnforceCacheLimit(g_cache_limit_mb);
    return localUtf8;
}