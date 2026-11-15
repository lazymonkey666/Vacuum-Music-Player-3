/*
* FILENAME:MAIN.CPP
* THIS IS THE MAIN FILE OF THE VACUUM MUSIC PLAYER
author:lazymonkey666(https://github.com/lazymonkey666;https://lazymonkey666.github.io/)

https://github.com/lazymonkey666/Vacuum-Music-Player-3 

license:GPL-3.0 license

2026.4 Version 3 (CPP VERSION)
*/

#define MINIAUDIO_IMPLEMENTATION

#ifndef SPI_SETTHEMES
#define SPI_SETTHEMES 0x001A
#endif

#include <platform/platform.h>

#include "miniaudio.h"
#include <stdio.h>
#include <random>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <iostream>



// ImGui 核心
#include <imgui/imgui.h>

// 项目内部（逻辑层）
#include <TagReader.h>
#include <ManageConfig.h>
#include <AMLLWebSocketV2.h>
#include <SettingItem.h>
#include <NeteaseAPIManager.h>

#include <IconManager.h>
#include <ThemePack.h>

//模块化播放源
#include <MusicSource.h>

//模块化播放后端
#include <PlaybackBackend.h>
#include <AsyncBackendBase.h>

//模块化播放服务
#include <PlaybackService.h>

//异步事件管理器
#include <AsyncEventManager.h>

//加载sonos模块
#include <SonosService.h>

#include <APPState.h>
#include <initialize.h>
#include <utils.h>
#include <MainUI.h>

#define HOTKEY_NEXT      1001
#define HOTKEY_PREV      1002
#define HOTKEY_PLAYPAUSE 1003
#define HOTKEY_SHOWHIDE  1004
#define HOTKEY_QUIT      1005

#define WM_APP_SMTC_PLAY   (WM_APP + 1)
#define WM_APP_SMTC_PAUSE  (WM_APP + 2)
#define WM_APP_SMTC_NEXT   (WM_APP + 3)
#define WM_APP_SMTC_PREV   (WM_APP + 4)
#define WM_UPDATE_ALBUM_ART   (WM_APP + 5)
#define WM_PLAYLIST_UPDATED (WM_APP + 6)

#define WM_FETCH_READY (WM_APP + 10)

#define WM_USER_SOFTWARE_RESTART (WM_APP+100)

#define WM_PLAY_READY  (WM_APP + 200)   // 播放准备就绪
#define WM_PLAY_FAILED (WM_APP + 201)   // 播放准备失败

#define WM_SONOS_STOPPED (WM_APP + 300)

static std::wstring coverPath;


void SaveAndRestart() {
    // 保存当前播放的歌曲
    cfg.Save();
#if PLATFORM_LINUX
    ReleaseSingleInstanceLock(); 
#endif
    RestartApplication();   // 平台函数
}



void UpdateWindowSide() {
    RECT rc = GetWindowRectPlat(g_hWnd);
    int screenWidth = GetScreenWidth();
    int centerX = rc.left + (rc.right - rc.left) / 2;
    g_windowSide = (centerX < screenWidth / 2) ? 0 : 1; // 0=左，1=右
    // 更新显示时的X坐标
    if (g_windowSide == 0) {
        g_displayX = 0;
    }
    else {
        g_displayX = screenWidth - (rc.right - rc.left);
    }
    g_displayY = rc.top;
}

void StartHideShowAnimation(bool hide) {
    if (g_isAnimating) return;
    RECT currentRect = GetWindowRectPlat(g_hWnd);
    int screenWidth = GetScreenWidth();
    int winWidth = currentRect.right - currentRect.left;
    int winHeight = currentRect.bottom - currentRect.top;

    // 默认目标位置为当前位置
    RECT targetRect = currentRect;
    BYTE targetAlpha = 255;

    // 记录当前尺寸
    g_animStartWidth = winWidth;
    g_animStartHeight = winHeight;

    if (hide) {
        targetAlpha = 0;
        if (g_enableSnapToEdge) {
            // 贴边停靠：移出屏幕，大小不变
            if (g_windowSide == 0) {
                targetRect.left = -winWidth;
                targetRect.right = 0;
            }
            else {
                targetRect.left = screenWidth;
                targetRect.right = screenWidth + winWidth;
            }
            g_animTargetWidth = winWidth;
            g_animTargetHeight = winHeight;
        }
        else {
            // 不贴边：位置不变，缩小到 0
            targetRect = currentRect; // 位置不变
            g_animTargetWidth = 0;
            g_animTargetHeight = 0;
        }
    }
    else { // 显示
        

        targetAlpha = 255;
        if (g_enableSnapToEdge) {
            // 贴边停靠：移回显示位置，大小不变
            targetRect.left = g_displayX;
            targetRect.right = g_displayX + winWidth;
            targetRect.top = g_displayY;
            targetRect.bottom = g_displayY + winHeight;
            g_animTargetWidth = winWidth;
            g_animTargetHeight = winHeight;
        }
        else {
            ShowWindowPlat(g_hWnd);
            // 不贴边：位置不变，放大到正常大小
            targetRect = currentRect;
            g_animTargetWidth = g_normalWidth;
            g_animTargetHeight = g_normalHeight;
        }
    }

    // 记录起始状态（位置和透明度）
    g_animStartRect = currentRect;
    g_animTargetRect = targetRect;
    g_animStartAlpha = g_currentAlpha;
    g_animTargetAlpha = targetAlpha;
    g_animStep = 0;
    g_isAnimating = true;
    g_targetHidden = hide;

    if (g_animTimer) StopWindowTimer(g_hWnd, g_animTimer);
    g_animTimer = StartWindowTimer(g_hWnd, 10);
}

std::vector<std::wstring> wsplit(const std::wstring& word, const std::wstring& s) {
    if (s.empty()) {
        throw std::invalid_argument("split(): empty separator");
    }

    std::vector<std::wstring> result;
    size_t start = 0;
    size_t pos = 0;

    while ((pos = word.find(s, start)) != std::wstring::npos) {
        // 提取从 start 到 pos 之间的子串（可能为空）
        result.push_back(word.substr(start, pos - start));
        start = pos + s.length();
    }

    // 添加最后一个片段（可能为空）
    result.push_back(word.substr(start));

    return result;
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
    const std::string& lyricsText = "")
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





//std::wstring s2ws(const std::string& str) {//万年老函数，老资历 ！
//    return Utf8ToPath(str);
//}



// 准备在线歌曲：下载、缓存、写入元数据，返回本地路径，失败返回空字符串
void PlayLocalFile(const std::string& filepath_utf8) {
    PathType widePath = Utf8ToPath(filepath_utf8);
    if (widePath.empty() || !ActiveBackend()) return;

    // 停掉旧播放（异步，不等待）
    if (ActiveBackend()->IsLoaded()) {
        ActiveBackend()->RequestUnload();
    }

    uint64_t myId = ++g_latestPreLoadId;

    AsyncEventPayload payload;
    payload.filePathUtf8 = filepath_utf8;
    payload.widePath = widePath;
    payload.trackIndex = CurrentIndex;
    payload.extraInt = myId;

    // ★ 有回调：PreLoad → 主线程拿结果 → 发起 Load
    AsyncEventManager::GetInstance().Post(
        AsyncEventType::PreLoad, std::move(payload),
        [](AsyncEventPayload& r, bool failed, bool) {
            if (r.extraInt != g_latestPreLoadId) return;   // 过期丢弃
            if (failed) {
                g_loading_music = false;
                return;
            }

            MusicMeta.title = r.title;
            MusicMeta.artist = r.artist;
            MusicMeta.album = r.album;

            // 3. 更新其他全局字符串和容器（直接使用 Set()）
            g_curTitle.Set(r.title);
            g_curArtist.Set(r.artist);
            g_curAlbum.Set(r.album);
            g_curLyrics.Set(r.lyrics);
            g_curAlbumArt.Set(r.albumArt);

            // 歌词
            g_lyrics = ParseLrc(r.lyrics);
            g_currentLyricIndex = -1;
            g_needScrollToLyric = false;

            // 时长（后端 load 后再刷新）
            PlayMusicLongMs = r.durationMs > 0 ? r.durationMs : 1;
            PlayMusicLong = PlayMusicLongMs / 1000.0f;

            // 异步 UI 封面（保持原逻辑）
            int taskId = ++g_AsyncTaskId_search;
            if (!r.albumArt.empty()) {
                auto raw = r.albumArt;
                std::string t = r.title, a = r.artist, al = r.album;
                std::thread([raw, t, a, al, taskId]() {
                    if (taskId != g_AsyncTaskId_search) return;
                    auto proc = ProcessAlbumArtWithGradient(raw, 230, 230);
                    if (proc.empty() || taskId != g_AsyncTaskId_search) return;
                    auto* result = new AlbumArtAsyncResult();
                    result->width = 230;
                    result->height = 230;
                    result->rgbaData = std::move(proc);
                    result->rawData = raw;
                    result->title = t;
                    result->artist = a;
                    result->album = al;
                    result->targetIndex = taskId;
                    PostMessage(g_hWnd, WM_UPDATE_ALBUM_ART, 0,
                        reinterpret_cast<LPARAM>(result));
                    }).detach();
            }
            else {
                auto* emptyResult = new AlbumArtAsyncResult();
                emptyResult->targetIndex = taskId;
                PostMessage(g_hWnd, WM_UPDATE_ALBUM_ART, 0,
                    reinterpret_cast<LPARAM>(emptyResult));
            }

            // ★ 发起后端 Load（taskId 存到全局，供 BackendLoaded 比对）
            if (auto* be = ActiveBackend()) {
                g_latestLoadTask = be->RequestLoad(r.widePath, r.trackIndex);
            }
        },
        CallbackThread::Main);
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

void PlayMusicFile(int index) {
    if (!g_source || index < 0 || index >= (int)g_source->GetCount()) return;

    g_loading_music = true;
    SetStatus(STATUS_FETCHING_URL);
    MainUI::g_scrollBlockCounter = 0;

    CurrentIndex = index;
    const BaseTrackInfo* track = g_source->GetBaseTrack(index);
    if (track) g_currentPlayingTrackId = track->id;
    g_source->SaveIndex(index);

    if (g_source) {
        int totalCount = (int)g_source->GetCount();

        // 计算安全的起始下标（防负数）
        int startIdx = index - 19;
        if (startIdx < 0) startIdx = 0;

        // 计算安全的结束下标（防越界）
        int endIdx = index + 30;
        if (endIdx > totalCount) endIdx = totalCount;

        // 触发预加载（底层异步且自带去重，频繁调用也没关系）
        g_source->RequestTrackDetails(startIdx, endIdx);
    }

    std::thread([index]() {
        try {
            // ★ 核心：优先检查本地缓存
            std::string finalLocalPath;
            const BaseTrackInfo* track = g_source ? g_source->GetBaseTrack(index) : nullptr;

            if (track) {
                PathType cacheW = GetCacheDirectory();
                PathType pathMp3 = cacheW + Utf8ToPath(track->id + ".mp3");
                PathType pathFlac = cacheW + Utf8ToPath(track->id + ".flac");
                std::error_code ec;

                if (std::filesystem::exists(pathFlac, ec) && !ec) {
                    finalLocalPath = PathToUtf8(pathFlac);
                }
                else if (std::filesystem::exists(pathMp3, ec) && !ec) {
                    finalLocalPath = PathToUtf8(pathMp3);
                }
            }

            // 如果命中缓存，直接发消息给主线程播放，立刻 return，不走任何网络！
            if (!finalLocalPath.empty()) {
                OutputDebugStringA("[PlayMusicFile] 命中缓存，秒播！\n");
                AsyncPlayResult result;
                result.success = true;
                result.index = index;
                result.localPath = finalLocalPath;
                PostMessage(g_hWnd, WM_PLAY_READY, 0, (LPARAM)new AsyncPlayResult(result));
                return; // 结束线程
            }

            // ================= 下面是没命中缓存的情况 =================
            SetStatus(STATUS_FETCHING_INFORMATION);
            PlaybackInfo info = g_source->PreparePlayback(index);

            if (!info.isValid) {
                throw std::runtime_error("获取播放信息失败");
            }

            std::string finalLocalPathNew = DownloadAndProcessTrack(info);
            if (finalLocalPathNew.empty()) {
                throw std::runtime_error("下载或处理失败");
            }

            AsyncPlayResult result;
            result.success = true;
            result.index = index;
            result.localPath = finalLocalPathNew;
            PostMessage(g_hWnd, WM_PLAY_READY, 0, (LPARAM)new AsyncPlayResult(result));
        }
        catch (const std::exception& e) {
            AsyncPlayResult errorRes;
            errorRes.success = false;
            errorRes.errorMsg = e.what();
            PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(errorRes));
        }
        catch (...) {
            AsyncPlayResult errorRes;
            errorRes.success = false;
            errorRes.errorMsg = "未知异常";
            PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(errorRes));
        }
        }).detach();
}

void UpdateCurrentPlaying(int new_index) {
    if (!g_source || g_source->GetCount() == 0) return;

    if (new_index < 0 || new_index >= (int)g_source->GetCount()) {
        CurrentIndex = 0;
    }
    else {
        CurrentIndex = new_index;
    }
    PlayMusicFile(CurrentIndex);
    IsPlaying = true;
}

void StopAnimation() {
    if (g_isAnimating) {
        if (g_animTimer) StopWindowTimer(g_hWnd, g_animTimer);
        g_animTimer = 0;
        g_isAnimating = false;

        // 强制恢复到正常显示状态（大小正常，透明度255）
        RECT rc = GetWindowRectPlat(g_hWnd);
        int w = g_normalWidth  > 0 ? g_normalWidth  : 700;
        int h = g_normalHeight > 0 ? g_normalHeight : 230;
        MoveResizeWindowPlat(g_hWnd, rc.left, rc.top, w, h);
        SetWindowAlpha(g_hWnd, 255);
        g_currentAlpha = 255;
        g_isWindowHidden = false;
        UpdateWindowSide();
    }
}

void initMusicList() {
    auto& cfg = ManageConfig::GetInstance();
    std::string playmode = cfg.GetValue<std::string>("play_mode", "local");

    g_source = MusicSourceRegistry::GetInstance().Create(playmode);
    if (!g_source) {
        // 兜底：本地
        g_source = MusicSourceRegistry::GetInstance().Create("local");
    }

    if (!g_source || !g_source->Init()) {
        ShowMessageBox("退出", "初始化播放列表失败，请检查网络或配置。");
        RequestQuitProgram();
        return;
    }

    CurrentIndex = g_source->GetLastIndex();
    if (g_source->GetCount() == 0) CurrentIndex = -1;
}

static void SnapToEdgeWithAnimation() {
    if (g_isAnimating) return;  // 动画进行中，避免冲突
    if (!g_enableSnapToEdge) return;
    RECT rc = GetWindowRectPlat(g_hWnd);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // 计算最近的边缘（左/右）
    int centerX = rc.left + winWidth / 2;
    int targetX;
    int targetSide;
    if (centerX < screenWidth / 2) {
        targetX = 0;           // 左边缘
        targetSide = 0;
    }
    else {
        targetX = screenWidth - winWidth;  // 右边缘
        targetSide = 1;
    }
    // 可选：保持垂直位置不变（或贴顶/底）
    int targetY = rc.top;  // 不改变垂直位置

    // 如果已经在目标位置，无需动画
    if (rc.left == targetX && rc.top == targetY) { UpdateWindowSide(); return; }


    // 复用动画变量
    g_animStartRect = rc;
    g_animTargetRect = { targetX, targetY, targetX + winWidth, targetY + winHeight };
    g_animStartAlpha = g_currentAlpha;
    g_animTargetAlpha = g_currentAlpha;  // 透明度不变
    g_animStep = 0;
    g_isAnimating = true;
    g_targetHidden = false;  // 不是隐藏动画

    // 更新显示位置（供隐藏/显示动画使用）
    g_animStartWidth = winWidth;
    g_animTargetWidth = winWidth;
    g_animStartHeight = winHeight;
    g_animTargetHeight = winHeight;

    g_displayX = targetX;
    g_displayY = targetY;
    g_windowSide = targetSide;

    if (g_animTimer) StopWindowTimer(g_hWnd, g_animTimer);
    g_animTimer = StartWindowTimer(g_hWnd, 10);
}

int Destroy(HWND hWnd) {
    cfg.Save();
    RECT rc = GetWindowRectPlat(hWnd);
    if (rc.right > rc.left && rc.bottom > rc.top) {
        int lastX = cfg.GetValue<int>("window_x", -1);
        int lastY = cfg.GetValue<int>("window_y", -1);
        if (lastX != rc.left || lastY != rc.top) {
            cfg.SetValue("window_x", rc.left);
            cfg.SetValue("window_y", rc.top);
            cfg.SetValue("window_side", g_windowSide);
            cfg.Save();
        }
    }


    PlaybackServiceRegistry::GetInstance().ShutdownAll();

    // 再清理后端
    PlaybackBackendRegistry::GetInstance().ShutdownAll();

    // ★ 停掉事件 worker
    AsyncEventManager::GetInstance().Stop();

    if (MusicMeta.albumArtSRV) {
        ReleaseTexture(MusicMeta.albumArtSRV);
        MusicMeta.albumArtSRV = nullptr;
    }
    StopWindowTimer(hWnd, g_animTimer);
    std::filesystem::remove(coverPath);
    CleanupNetwork();
    UnregisterSessionNotification();
    UnregisterAllHotkeys();
    RequestQuitProgram();
    return 0;
}

bool ChangePlayMode() {
    auto* be = ActiveBackend();
    if (!be) return IsPlaying;

    if (!IsPlaying) {
        g_latestPlayTask = be->RequestPlay(CurrentIndex);
        // IsPlaying 由 BackendPlayed 事件设置（保持原逻辑），这里乐观设置避免 UI 闪烁
        IsPlaying = true;
    }
    else {
        be->RequestPause(CurrentIndex);
        IsPlaying = false;
    }
    return IsPlaying;
}

void AutoPlayNextSong() {
    if (!g_source) return;
    size_t trackCount = g_source->GetCount();
    if (trackCount == 0) return;

    int nextIndex = CurrentIndex;
    if (g_PlayOrder == 0) {
        nextIndex = (CurrentIndex + 1) % trackCount;
    }
    else if (g_PlayOrder == 1) {
        nextIndex = CurrentIndex;
    }
    else if (g_PlayOrder == 2) {
        // 原有的随机逻辑，需要根据 g_source->GetBaseTrack(i)->id 来比对
        if (g_RemainPlaylist.empty()) {
            g_RemainPlaylist.clear();
            for (size_t i = 0; i < trackCount; ++i) g_RemainPlaylist.push_back(g_source->GetBaseTrack(i)->id);
        }
        auto it = std::find(g_RemainPlaylist.begin(), g_RemainPlaylist.end(), g_source->GetBaseTrack(CurrentIndex)->id);
        if (it != g_RemainPlaylist.end()) g_RemainPlaylist.erase(it);
        if (g_RemainPlaylist.empty()) {
            for (size_t i = 0; i < trackCount; ++i) g_RemainPlaylist.push_back(g_source->GetBaseTrack(i)->id);
        }
        int r = rand() % (int)g_RemainPlaylist.size();
        std::string nextSongId = g_RemainPlaylist[r];

        for (size_t i = 0; i < trackCount; ++i) {
            if (g_source->GetBaseTrack(i)->id == nextSongId) {
                nextIndex = (int)i;
                break;
            }
        }
    }
    UpdateCurrentPlaying(nextIndex);
}

// 根据 mode 和系统状态，算出该用深色还是浅色背景
bool ResolveIsDark(const std::string& mode) {
    if (mode == "Dark")   return true;
    if (mode == "Light")  return false;
    return IsDarkModeByRegistry();   // System
}

// 应用主题（纯副作用，无返回值）
void ApplyTheme(const std::string& mode, int opacity) {
    bool isDark = ResolveIsDark(mode);
    if (isDark) {
        EnableAcrylic(g_hWnd, (uint8_t)opacity, 0x222222);
        g_iconColor = 0xFFFFFF;
    }
    else {
        EnableAcrylic(g_hWnd, (uint8_t)opacity, 0xFFFFFF);
        g_iconColor = 0x222222;
    }
}

// 获取图标颜色（纯函数，无副作用）
uint32_t GetIconColorForTheme(const std::string& mode) {
    return ResolveIsDark(mode) ? 0xFFFFFF : 0x222222;
}

void OnReadyPlay(AsyncPlayResult* result) {
    if (!result) return;

    if (result->success) {
        CurrentIndex = result->index;

        // ★ 全异步流程入口：PreLoad → BackendLoaded → BackendPlayed
        //   注意：这里【不清】g_loading_music
        PlayLocalFile(result->localPath);
    }
    else {
        OutputDebugStringA(("播放失败: " + result->errorMsg + "\n").c_str());
        g_loading_music = false;   // 失败立刻清
    }

    delete result;
}
void OnPlayFailed(AsyncPlayResult* result) {
    if (!result) return;
    OutputDebugStringA(("准备失败: " + result->errorMsg + "\n").c_str());
    g_loading_music = false;
    delete result;

    // ★ 自动切歌与防死循环
    static int consecutiveFails = 0;
    static uint64_t lastFailTime = 0;

    uint64_t now = GetTickCount64();
    // 如果距离上一次失败超过 5 秒，说明用户隔了很久才点，重置计数器
    if (now - lastFailTime > 5000) {
        consecutiveFails = 0;
    }
    lastFailTime = now;

    if (consecutiveFails < 10) {
        consecutiveFails++;
        OutputDebugStringA(("[OnPlayFailed] 连续失败 " + std::to_string(consecutiveFails) + " 次，尝试自动切换下一首\n").c_str());
        // 延时切歌，防止请求太快导致服务器风控，也避免 UI 瞬间乱跳
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            PostMessage(g_hWnd, WM_APP_SMTC_NEXT, 0, 0); // 或者直接调 AutoPlayNextSong()
            }).detach();
    }
    else {
        consecutiveFails = 0;
        ShowMessageBox("播放停止", "连续多首歌曲无法播放已停止自动跳过。");
    }
}
void OnMediaNext() {
    if (!g_source || g_source->GetCount() == 0) return;
    if (CurrentIndex + 1 < (int)g_source->GetCount()) {
        AutoPlayNextSong();
    }
    else {
        UpdateCurrentPlaying(0);
    }
}


void OnMediaPrev() {
    if (!g_source || g_source->GetCount() == 0) return;
    if (CurrentIndex - 1 >= 0) {
        UpdateCurrentPlaying(CurrentIndex - 1);
    }
    else {
        UpdateCurrentPlaying((int)g_source->GetCount() - 1);
    }
}
void OnHotkey(int id) {
    OutputDebugStringA(("[hotkey] OnHotkey id=" + std::to_string(id) + "\n").c_str());
    switch (id) {
    case HOTKEY_NEXT:       OnMediaNext();       break;
    case HOTKEY_PREV:       OnMediaPrev();       break;
    case HOTKEY_PLAYPAUSE:  ChangePlayMode();    break;
    case HOTKEY_SHOWHIDE:   StartHideShowAnimation(!g_isWindowHidden); break;
    case HOTKEY_QUIT:       RequestQuit();       break;  // ← 见下方说明
    }
}

void OnDragEnd() {
    if (!g_isDragging) return;
    StopAnimation();
    g_isDragging = false;
    ReleaseMouseCapture();             // ← 这个是平台相关，见下方
    SnapToEdgeWithAnimation();
}

void OnAlbumArtReady(AlbumArtAsyncResult* result) {
    if (!result) return;

    if (result->targetIndex == g_AsyncTaskId_search) {
        // 1. 移交旧图
        if (MainUI::g_prevAlbumArt) {
            ReleaseTexture(MainUI::g_prevAlbumArt);
            MainUI::g_prevAlbumArt = nullptr;
        }
        if (MusicMeta.albumArtSRV) {
            MainUI::g_prevAlbumArt = MusicMeta.albumArtSRV;
            MusicMeta.albumArtSRV = nullptr;
        }

        // 2. 创建新图
        if (!result->rgbaData.empty()) {
            MusicMeta.albumArtSRV = CreateTextureFromRGBA(result->rgbaData, result->width, result->height);
        }

        // 3. 启动动画
        MainUI::g_albumArtFadeAlpha = 0.0f;
        MainUI::g_isAlbumArtTransitioning = true;
        MainUI::g_albumArtTransStartTime = std::chrono::steady_clock::now();
    }
    delete result;
}

void OnAnimationTick() {
    g_animStep++;
    float t = (float)g_animStep / g_animTotalSteps;

    if (t >= 1.0f) {
        StopWindowTimer(g_hWnd, g_animTimer);
        g_animTimer = 0;
        g_isAnimating = false;
    
        int finalW = g_animTargetWidth;
        int finalH = g_animTargetHeight;
        if (finalW < 1) finalW = 1;      // ★ 加这两行
        if (finalH < 1) finalH = 1;
    
        MoveResizeWindowPlat(g_hWnd,
            g_animTargetRect.left, g_animTargetRect.top,
            finalW, finalH);

        SetWindowAlpha(g_hWnd, 255);
        if (g_targetHidden) {
            if (!g_enableSnapToEdge){
                HideWindowPlat(g_hWnd);
            }
            OutputDebugStringA("[hide] window hidden\n");
        }
        g_currentAlpha = g_animTargetAlpha;
        g_isWindowHidden = g_targetHidden;
        UpdateWindowSide();
        return;
    }

    float eased = g_targetHidden
        ? t * t * t
        : 1.0f - powf(1.0f - t, 3);

    int x = g_animStartRect.left + (int)((g_animTargetRect.left - g_animStartRect.left) * eased);
    int y = g_animStartRect.top + (int)((g_animTargetRect.top - g_animStartRect.top) * eased);
    int width = g_animStartWidth + (int)((g_animTargetWidth - g_animStartWidth) * eased);
    int height = g_animStartHeight + (int)((g_animTargetHeight - g_animStartHeight) * eased);
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    BYTE alpha = (BYTE)(g_animStartAlpha + (g_animTargetAlpha - g_animStartAlpha) * eased);
    MoveResizeWindowPlat(g_hWnd, x, y, width, height);
    SetWindowAlpha(g_hWnd, alpha);
    g_currentAlpha = alpha;
}
// ═══════════════════════════════════════════════════════════
// 业务消息分发（跨平台，WndProc 和 LnxProc 都调它）
// ═══════════════════════════════════════════════════════════
bool HandleAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PLAY_READY:
        OnReadyPlay(reinterpret_cast<AsyncPlayResult*>(lParam));
        return true;

    case WM_PLAY_FAILED:
        OnPlayFailed(reinterpret_cast<AsyncPlayResult*>(lParam));
        return true;

    case WM_UPDATE_ALBUM_ART:
        OnAlbumArtReady(reinterpret_cast<AlbumArtAsyncResult*>(lParam));
        return true;

    case WM_HOTKEY:
        OnHotkey((int)wParam);
        return true;

    case WM_APP_SMTC_PLAY:
    case WM_APP_SMTC_PAUSE:
        ChangePlayMode();
        return true;

    case WM_APP_SMTC_NEXT:
        OnMediaNext();
        return true;

    case WM_APP_SMTC_PREV:
        OnMediaPrev();
        return true;

    case WM_PLAYLIST_UPDATED:
        if (g_source && !g_currentPlayingTrackId.Get().empty()) {
            // 当歌单发生变动（增删），使用 ID 找回它最新的位置
            int newIdx = g_source->FindTrackIndex(g_currentPlayingTrackId.Get());
            if (newIdx >= 0 && newIdx != CurrentIndex) {
                CurrentIndex = newIdx;
            }
        }
        return true;


    case WM_TIMER:
        if (wParam == 12345 && g_isAnimating) {
            OnAnimationTick();
        }
        return true;

    
    }return false;  // 不是业务消息
}


#if PLATFORM_WINDOWS
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    //处理自定义消息
    //if (msg == g_uTaskbarBtnMsg) {
    //    if (g_pTaskbarList) {
    //        g_pTaskbarList->HrInit();
    //    }
    //    return 0;
    //}
    if (HandleAppMessage(msg, wParam, lParam))
        return 0;

    switch (msg) {
    case WM_CREATE:
    {
        // 使用 LoadIcon 加载资源文件中的图标
        // 第二个参数 MAKEINTRESOURCE(IDI_ICON1) 是资源的ID，
        // 请将 IDI_ICON1 替换为你自己 `resource.h` 中定义的ID
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
        if (hIcon)
        {
            // 发送 WM_SETICON 消息来设置窗口的大图标（ALT+TAB时显示）和小图标（标题栏左上角）
            SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
        break;
    }
    case WM_SETTINGCHANGE:
    {
        ApplyTheme(g_dark_mode.Get(), g_opacity);
        IconManager::GetInstance().SetThemeColor(g_iconColor);
        g_needUpdateStyle = true;
        InvalidateRect(hWnd, NULL, TRUE);
    }
    break;   // 必须 break，防止穿透
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        // 强调色改变时（Windows 10/11）
        g_needUpdateStyle = true;
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    case WM_SIZE:
        // 窗口大小改变时调整交换链缓冲区
        if (g_pSwapChain && wParam != SIZE_MINIMIZED) {
            if (g_mainRenderTargetView) {
				g_mainRenderTargetView->Release();
                g_mainRenderTargetView = nullptr;
            }
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            // 重新创建渲染目标视图
            ID3D11Texture2D* pBackBuffer = nullptr;
            if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
                g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
				pBackBuffer->Release();
            }   
        }
        break;


    case WM_RBUTTONDOWN: {
        // 右键拖动：无视任何控件，直接启动
        g_isDragging = true;
        GetCursorPos(&g_dragStartPos);
        RECT rc;
        GetWindowRect(hWnd, &rc);
        g_windowStartPos.x = rc.left;
        g_windowStartPos.y = rc.top;
        CaptureMouse(hWnd);
        return 0;  // 阻止右键菜单
    }

    case WM_MOUSEMOVE: {
        if (g_isDragging) {
            POINT curPos;
            GetCursorPos(&curPos);
            int dx = curPos.x - g_dragStartPos.x;
            int dy = curPos.y - g_dragStartPos.y;
            int newX = g_windowStartPos.x + dx;
            int newY = g_windowStartPos.y + dy;

            // 获取屏幕工作区（排除任务栏）
            RECT workArea;
            SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
            int screenLeft = workArea.left;
            int screenRight = workArea.right;
            int screenTop = workArea.top;
            int screenBottom = workArea.bottom;
            RECT rc;
            GetWindowRect(hWnd, &rc);
            int winWidth = rc.right - rc.left;
            int winHeight = rc.bottom - rc.top;

            // 水平贴边
            if (g_enableSnapToEdge) {
                if (newX <= screenLeft + g_snapThreshold) {
                    newX = screenLeft;
                }
                else if (newX + winWidth >= screenRight - g_snapThreshold) {
                    newX = screenRight - winWidth;
                }
                // 垂直贴边（可选）
                if (newY <= screenTop + g_snapThreshold) {
                    newY = screenTop;
                }
                else if (newY + winHeight >= screenBottom - g_snapThreshold) {
                    newY = screenBottom - winHeight;
                }
            }
            MoveWindowPlat(hWnd, newX, newY);

            // ===== 添加以下三行 =====
            UpdateWindowSide();                     // 更新显示位置和停靠侧
            GetWindowRect(hWnd, &rc);               // 重新获取窗口位置
            g_windowStartPos.x = rc.left;           // 更新拖动起点
            g_windowStartPos.y = rc.top;
            g_dragStartPos = curPos;                // 更新鼠标起点
            return 0;
        }
        break;
    }
    
    case WM_MOUSEWHEEL: {
        break;
    }
    case WM_RBUTTONUP:
    case WM_LBUTTONUP: {
        if (g_isDragging) {
            OnDragEnd();
            return 0;
        }
        break;
    }
    
    break;
    
    case WM_DESTROY:
        return Destroy(hWnd);
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

#endif

bool seek(float seekRatio) {
    auto* be = ActiveBackend();
    if (!be || !be->IsLoaded()) return false;

    seekRatio = std::clamp(seekRatio, 0.0f, 1.0f);
    uint64_t posMs = (uint64_t)(PlayMusicLongMs * seekRatio);

    be->RequestSeek(posMs);

    // 乐观更新 UI（后端 Seeked 事件会再刷新）
    PlayCurrentMs = posMs;
    PlayProgressPercent = seekRatio;
    return true;
}


static void RegisterAsyncHooks() {
    auto& em = AsyncEventManager::GetInstance();

    // ── 全局兜底：任何事件失败都打日志 ──
    em.SetGlobalFallback([](AsyncEventPayload& p, const std::string& reason) {
        OutputDebugStringA(("[AsyncFallback] " + reason +
            " track=" + std::to_string(p.trackIndex) + "\n").c_str());
        });

    // ═══════════════════════════════════════════════════════
    // PreLoad：读元数据 / 歌词 / 封面（worker 线程执行）
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::PreLoad,
        [](AsyncEventPayload& p) -> EventOutcome {
            if (p.widePath.empty()) return EventOutcome::Failed;

            // 元数据
            auto meta = GetMusicMetadata(p.filePathUtf8);
            p.title = meta.title;
            p.artist = meta.artist;
            p.album = meta.album;

            // 歌词（ID3 优先，其次 .lrc）
            std::string lrc = GetLyricsFromFile(p.widePath);
            if (lrc.empty()) {
                PathType lp = FindLrcFile(p.widePath);
                if (!lp.empty()) {
                    lrc = ReadTextFile(lp);
                    if (lrc.size() >= 3 &&
                        (unsigned char)lrc[0] == 0xEF &&
                        (unsigned char)lrc[1] == 0xBB &&
                        (unsigned char)lrc[2] == 0xBF) {
                        lrc = lrc.substr(3);
                    }
                }
            }
            p.lyrics = lrc;
            p.albumArt = ExtractAlbumArt(p.widePath);
            return EventOutcome::Handled;
        });

    // ═══════════════════════════════════════════════════════
    // BackendLoaded：后端加载成功 → 发起播放
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendLoaded,
        [](AsyncEventPayload& p) -> EventOutcome {
            if (p.extraInt != g_latestLoadTask) return EventOutcome::Skipped;

            if (auto* be = ActiveBackend()) {
                g_latestPlayTask = be->RequestPlay(p.trackIndex);
            }
            return EventOutcome::Handled;
        });

    // ═══════════════════════════════════════════════════════
    // BackendPlayed：后端启动播放
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendPlayed, [](AsyncEventPayload& p) -> EventOutcome {
        if (p.extraInt != g_latestPlayTask) return EventOutcome::Skipped;
        IsPlaying = true;
        g_loading_music = false;
        PlayCurrentMs = p.positionMs;
        PlayMusicLongMs = p.durationMs > 0 ? p.durationMs : PlayMusicLongMs;

        // ★ 回填 PreLoad 阶段拿到的元数据
        p.title = g_curTitle.Get();
        p.artist = g_curArtist.Get();
        p.album = g_curAlbum.Get();
        p.lyrics = g_curLyrics.Get();
        p.albumArt = g_curAlbumArt.Get();
        if (p.durationMs == 0) p.durationMs = PlayMusicLongMs;

        AsyncEventManager::GetInstance().Post(AsyncEventType::PostPlay, p);
        return EventOutcome::Handled;
        });

    // ═══════════════════════════════════════════════════════
    // BackendLoadFailed：加载失败 → UI 提示 + 状态复位
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendLoadFailed,
        [](AsyncEventPayload& p) -> EventOutcome {
            if (p.extraInt != g_latestLoadTask) return EventOutcome::Skipped;
            OutputDebugStringA(("[LoadFailed] " + p.errorMsg + "\n").c_str());
            g_loading_music = false;
            return EventOutcome::Handled;
        });

    // ═══════════════════════════════════════════════════════
    // BackendEnded：自然结束 → 自动下一首
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendEnded,
        [](AsyncEventPayload&) -> EventOutcome {
            AutoPlayNextSong();
            return EventOutcome::Handled;
        });

    em.Subscribe(AsyncEventType::ExternalTogglePlay,
        [](AsyncEventPayload&) -> EventOutcome { ChangePlayMode(); return EventOutcome::Handled; });

    em.Subscribe(AsyncEventType::ExternalNext,
        [](AsyncEventPayload&) -> EventOutcome { OnMediaNext(); return EventOutcome::Handled; });

    em.Subscribe(AsyncEventType::ExternalPrev,
        [](AsyncEventPayload&) -> EventOutcome { OnMediaPrev(); return EventOutcome::Handled; });

    em.Subscribe(AsyncEventType::ExternalSeek,
        [](AsyncEventPayload& p) -> EventOutcome {
            double ratio = (double)p.positionMs / PlayMusicLongMs;
            seek((float)std::clamp(ratio, 0.0, 1.0));
            return EventOutcome::Handled;
        });


}



#if PLATFORM_WINDOWS

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hMutex = CreateMutexA(NULL, FALSE, "LazyMonkey666_VacuumMusicPlayer_Mutex");
    if (g_hMutex == NULL) {
        // 创建失败（系统错误），可以继续或退出，但通常不会发生
        ShowMessageBox("错误", "程序创建失败，程序可能已运行");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已有实例在运行，弹出提示并退出
        ShowMessageBox("提示", "程序已经在运行中");
        CloseHandle(g_hMutex);
        return 0;
    }
    const std::string CLASS_NAME = "VacuumMusicPlayer";
    HRESULT hr = SetCurrentProcessExplicitAppUserModelID(L"LazyMonkey666.VacuumMusicPlayer");
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        ShowMessageBox("错误", "WSAStartup 失败！");
        return 1;
    }

    RegisterAsyncHooks();          // ★ 新增：注册事件订阅
    AsyncEventManager::GetInstance().Start();   // ★ 启动 worker
    PlaybackServiceRegistry::GetInstance().EnableDefaults();//启动服务

    if (!g_apiAddr.Get().empty() && g_online_player) {
        NeteaseAPIManager::GetInstance().SetBaseURL(g_apiAddr.Get());
    }
    else if(g_online_player) {
        ShowMessageBox("退出", "未配置网易云歌单ID或API地址配置错误（有可能已经失效），程序将退出");
        PostMessage(g_hWnd, WM_CLOSE, 0, 0);
    }
    srand(time(NULL));
    if (FAILED(hr)) {
        // 如果设置失败，可以输出调试信息，但不影响程序继续运行
        OutputDebugString(L"Failed to set AppUserModelID\n");
    }

    if (g_source) {
        g_source->SyncPlaylistAsync();
    }
    if (!AppInit::InitCore()) return 1;
    try { winrt::init_apartment(); }//winrt初始化
    catch (...) { OutputDebugStringA("winrt没有初始化\n"); }
    static SettingWindow settings;
    if (!AppInit::InitUI(CLASS_NAME)) return 1;
    if (ThemePackManager::GetInstance().GetActive()) {
        g_iconCache.clear(); // 清理可能提前生成的默认图标
        MainUI::UpdateImGuiStyle();  // 强制把主题颜色应用进 ImGuiStyle
        // 如果主题包里图标颜色需要跟随主题，这里也可以重新设置
        // IconManager::GetInstance().SetThemeColor(g_iconColor); 
    }
    UpdateCurrentPlaying(CurrentIndex);
    InitTaskbarProgress(g_hWnd);
    IconManager::GetInstance().Initialize();
    IconManager::GetInstance().SetThemeColor(g_iconColor);

    

    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    g_normalWidth = rc.right - rc.left;
    g_normalHeight = rc.bottom - rc.top;


    int counter = 0;
    MSG msg{};
    //进入消息循环
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        AsyncEventManager::GetInstance().DrainMainThread();
        if (g_needUpdateStyle) {
            g_iconCache.clear();
            ApplyTheme(g_dark_mode.Get(), g_opacity);
            IconManager::GetInstance().SetThemeColor(g_iconColor);
            MainUI::UpdateImGuiStyle();
            g_needUpdateStyle = false;


            // 如果主题里涉及背景图，也可以在这里重新加载
            // ThemePackManager::GetInstance().LoadActiveBackgroundTexture();
        }
        if (ActiveBackend() && ActiveBackend()->IsLoaded() && !g_lyrics.empty()) {
            ma_uint64 currentMs = PlayCurrentMs;
            // 找到最后一个时间 <= currentMs 的行
            int newIndex = -1;
            for (int i = 0; i < (int)g_lyrics.size(); ++i) {
                if (g_lyrics[i].first <= currentMs) {
                    newIndex = i;
                }
                else {
                    break;
                }
            }
            if (newIndex != g_currentLyricIndex) {
                g_currentLyricIndex = newIndex;
                g_needScrollToLyric = true;
            }
        }
        // 检查是否超时（3秒无操作）
        counter++;
        if (counter >= 60) {
            counter = 0;
        }

        if (MainUI::g_scrollBlockCounter > 0) {
            MainUI::g_scrollBlockCounter--;
        }
        if (counter % 60 == 0) {   // 每秒一次
            PlaybackServiceRegistry::GetInstance().Tick(PlayCurrentMs, IsPlaying);
        }
        
        if (!g_pd3dDevice || !g_pSwapChain || !g_mainRenderTargetView || !g_pd3dDeviceContext)
        {
            // D3D资源残缺，暂时不渲染，等待重建完成
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (g_isWindowHidden && !g_isAnimating) {
            // 隐藏且无动画时限制帧率
            static auto lastRender = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRender).count();
            if (elapsed < 50) { // 20fps
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            lastRender = now;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        // 渲染 ImGui 界面
        MainUI::RenderFrame(counter, settings);
        
        const float clear[4] = { 0, 0, 0, 0 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        // 检测 D3D11 设备丢失
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            // 释放 ImGui D3D11 后端资源
            ImGui_ImplDX11_Shutdown();
            // 释放原有 D3D 资源
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
            if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
            if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
            if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

            // 重新创建设备
            if (!CreateDeviceD3D(g_hWnd)) {
                ShowMessageBox("错误", "D3D 设备恢复失败，程序将退出");
                PostQuitMessage(0);
                break;
            }
            // 重新初始化 ImGui D3D11 后端
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
            // 注意：ImGui_ImplWin32_Init 不需要重新调用，窗口句柄未变
        }

    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_mainRenderTargetView) g_mainRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();

    CleanupWindowPlat();
    return 0;
}

#endif
#if PLATFORM_LINUX
bool LnxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // ─── 业务消息（和 WndProc 里完全一致）───
    if (HandleAppMessage(msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SETTINGCHANGE:
        g_needUpdateStyle = true;
        return true;

    case WM_RBUTTONDOWN: {
        g_isDragging = true;
        g_dragStartPos = GetMousePosPlat();          // 鼠标按下时的屏幕坐标
        RECT rc = GetWindowRectPlat(hWnd);
        g_windowStartPos.x = rc.left;                // 窗口按下时的屏幕位置
        g_windowStartPos.y = rc.top;
        CaptureMouse(hWnd);
        return true;
    }

    case WM_MOUSEMOVE: {
        if (g_isDragging) {
            POINT curPos = GetMousePosPlat();

            // ★ 绝对拖拽：始终基于"按下时的基准"计算，不累加
            int dx = curPos.x - g_dragStartPos.x;
            int dy = curPos.y - g_dragStartPos.y;
            int newX = g_windowStartPos.x + dx;
            int newY = g_windowStartPos.y + dy;

            // 吸附
            RECT workArea = GetWorkAreaPlat();
            RECT rc = GetWindowRectPlat(hWnd);
            int winWidth  = rc.right - rc.left;
            int winHeight = rc.bottom - rc.top;

            if (g_enableSnapToEdge) {
                if (newX <= workArea.left + g_snapThreshold) newX = workArea.left;
                else if (newX + winWidth >= workArea.right - g_snapThreshold)
                    newX = workArea.right - winWidth;
                if (newY <= workArea.top + g_snapThreshold) newY = workArea.top;
                else if (newY + winHeight >= workArea.bottom - g_snapThreshold)
                    newY = workArea.bottom - winHeight;
            }

            MoveWindowPlat(hWnd, newX, newY);

            // ★ 关键：不要更新 g_dragStartPos / g_windowStartPos！
            //    它们保持"按下时的基准"不变，这样每次算的都是绝对位置
            return true;
        }
        break;
    }

    case WM_MOUSEWHEEL:
        break;

    case WM_RBUTTONUP:
    case WM_LBUTTONUP:
        if (g_isDragging) {
            OnDragEnd();
            return true;
        }
        break;

    case WM_CLOSE:
        Destroy(hWnd);
        return true;
    }

    return false;
}

int main(int argc, char** argv) {
    std::cout << "[Test] argc = " << argc << std::endl;
    for (int i = 0; i < argc; i++) {
        std::cout << "[Test] argv[" << i << "] = " << argv[i] << std::endl;
    }
    OutputDebugStringA("[main] Vacuum Music Player (Linux) starting...\n");
    const std::string CLASS_NAME = "VacuumMusicPlayer";
#if _DEBUG
    setenv("DISPLAY", ":0", 1);
#endif
    const char* display = std::getenv("DISPLAY");
    std::cout << "[Debug] Current DISPLAY env: " << (display ? display : "NULL") << std::endl;
    signal(SIGPIPE, SIG_IGN);

    if (!AcquireSingleInstanceLock()) {
        ShowMessageBox("提示", "程序已经在运行中");
        return 0;
    }

    if (!InitNetwork()) {
        ShowMessageBox("错误", "网络初始化失败");
        return 1;
    }

    ReadConfig();
    RegisterMusicSources();


    if (!g_apiAddr.empty() && g_online_player) {
        NeteaseAPIManager::GetInstance().SetBaseURL(g_apiAddr);
    }

    if (!g_sonos) {
        g_sonos = std::make_unique<SonosController>();
        g_sonos->Initialize();
    }
    srand(time(NULL));
    initMusicList();
    if (!InitPlayerEngine()) return 1;

    InitUserCookie();
    static SettingWindow settings;

    if (!InitWindow(CLASS_NAME)) return 1;
    InitHotkey(g_hWnd);
    

    // TODO: 接 GLFW + ImGui OpenGL3 后端
    PrepareImGui();
    LoadTheme();
    if (ThemePackManager::GetInstance().GetActive()) {
        g_iconCache.clear(); // 清理可能提前生成的默认图标
        UpdateImGuiStyle();  // 强制把主题颜色应用进 ImGuiStyle
        // 如果主题包里图标颜色需要跟随主题，这里也可以重新设置
        // IconManager::GetInstance().SetThemeColor(g_iconColor); 
    }

    UpdateCurrentPlaying(CurrentIndex);
    StartSearchSonos();

    int counter = 0;
    MSG msg{};
    while (msg.message != WM_QUIT) {
        LnxProcessTimers();
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            LnxProc(g_hWnd, msg.message, msg.wParam, msg.lParam);   // LnxProc 内部会先调 HandleAppMessage
            continue;
        }
        if (g_needUpdateStyle) {
            g_iconCache.clear();
            ApplyTheme(g_dark_mode, g_opacity);
            IconManager::GetInstance().SetThemeColor(g_iconColor);
            UpdateImGuiStyle();
            g_needUpdateStyle = false;


            // 如果主题里涉及背景图，也可以在这里重新加载
            // ThemePackManager::GetInstance().LoadActiveBackgroundTexture();
        }

        if (!g_loading_music) {
            if (ma_sound_at_end(&g_CurrentSound) == MA_TRUE && IsPlaying == true && !g_sonos_connect) {
                ma_sound_stop(&g_CurrentSound);
                AutoPlayNextSong();
            }
        }
        LnxPollEvents();
        LnxPollMouse();
        LnxPollX11Events();   


        // 歌词滚动逻辑
        if ((g_IsSoundLoaded && !g_lyrics.empty()) ||
            (g_sonos_connect && !g_loading_music && !g_lyrics.empty())) {
            ma_uint64 currentMs = PlayCurrentMs;
            int newIndex = -1;
            for (int i = 0; i < (int)g_lyrics.size(); ++i) {
                if (g_lyrics[i].first <= currentMs) newIndex = i;
                else break;
            }
            if (newIndex != g_currentLyricIndex) {
                g_currentLyricIndex = newIndex;
                g_needScrollToLyric = true;
            }
        }

        counter++;
        if (counter >= 60) counter = 0;
        if (g_scrollBlockCounter > 0) g_scrollBlockCounter--;

        if (amllplayer && counter == 0) {
            amllplayer->sendProgress(PlayCurrentMs);
            if (IsPlaying == false) amllplayer->sendPaused();
            else amllplayer->sendResumed();
        }

        if (amllplayer && counter == 0) {
            AMLLCommand cmd = amllplayer->getNextCommand();
            switch (cmd) {
                case AMLLCommand::Pause:    ChangePlayMode(); break;
                case AMLLCommand::Resume:   ChangePlayMode(); break;
                case AMLLCommand::Forward:  CurrentIndex++; UpdateCurrentPlaying(CurrentIndex); break;
                case AMLLCommand::Backward: CurrentIndex--; UpdateCurrentPlaying(CurrentIndex); break;
                case AMLLCommand::Seek: {
                    uint64_t seekPos = amllplayer->getPendingSeek();
                    if (seekPos == UINT64_MAX) break;
                    double ratio = static_cast<double>(seekPos) / PlayMusicLongMs;
                    if (ratio < 0.0) ratio = 0.0;
                    if (ratio > 1.0) ratio = 1.0;
                    seek(ratio);
                    break;
                }
            }
        }
        if (!g_isWindowHidden) {
            LnxBeginFrame();
            RenderFrame(counter, settings);
            LnxEndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    CleanupNetwork();
    return 0;
}


#endif // PLATFORM_LINUX