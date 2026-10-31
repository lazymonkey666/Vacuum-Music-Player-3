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

// TagLib 全家桶
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/id3v2framefactory.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/flacpicture.h>

#include <filedialog.cpp>

// ImGui 核心
#include <imgui/imgui.h>

// 项目内部（逻辑层）
#include <albumimgtagreader.h>
#include <manageconfig.h>
#include <AMLLWebSocketV2.h>
#include <SonosController.h>
#include <SettingItem.h>
#include <NeteaseAPIManager.h>

#include <IconManager.h>
#include <ThemePack.h>
#include <MusicSource.h>
#include <iostream>

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

#define WM_FETCH_READY (WM_APP + 10)

#define WM_USER_SOFTWARE_RESTART (WM_APP+100)

#define WM_PLAY_READY  (WM_APP + 200)   // 播放准备就绪
#define WM_PLAY_FAILED (WM_APP + 201)   // 播放准备失败

#define WM_SONOS_STOPPED (WM_APP + 300)


// --- 播放源指针 ---
static std::unique_ptr<IMusicSource> g_source = nullptr;
struct AsyncFetchResult {
    int taskId;
    PlaybackInfo info;
};

static int CurrentIndex = -1;          // 当前播放歌曲在 MusicFiles 中的索引
static float PlayProgressPercent = 0.0f;// 播放进度（百分比）
static ma_uint64  PlayCurrentMs = 0;// 播放进度（时长）
static float PlayMusicLong = 0.0f;      // 总时长
static bool IsPlaying = false;         // 是否正在播放
static ma_uint64 PlayMusicLongMs = 1;
//static std::vector<std::string> MusicFiles;
static ma_uint64 g_TotalPCMFrames = 0; //音乐PCM总帧数
static bool g_NeedScrollToCurrent = false;  // 滚动标志
static int g_AsyncTaskId_search = 0;   // 统一的全局任务ID
static int g_AsyncTaskId_download = 0;


//动画相关变量-begin
static bool g_isAnimating = false;          // 是否正在动画
static bool g_targetHidden = false;         // 目标状态：true=隐藏，false=显示
static int g_animStep = 0;                  // 当前步数
static int g_animTotalSteps = 15;            // 总步数
static RECT g_animStartRect = { 0 };           // 起始位置
static RECT g_animTargetRect = { 0 };          // 目标位置
static BYTE g_animStartAlpha = 255;          // 起始透明度
static BYTE g_animTargetAlpha = 255;         // 目标透明度
static UINT_PTR g_animTimer = 0;             // 定时器ID
static bool g_isWindowHidden = false;        // 当前是否隐藏（用于热键/按钮判断）
static int g_displayX = 0;                   // 窗口显示时的X坐标（根据停靠侧）
static int g_displayY = 0;                   // 显示时的Y坐标（保持当前Y）
static int g_windowSide = 0;
static int g_currentAlpha = 255;
//动画相关变量-end

static int g_snapThreshold = 10;      // 吸附阈值（像素）
static int g_lastSnapSide = -1;       // 0=左, 1=右, 2=顶, 3=底（仅左右即可）

// 歌词相关
static std::vector<std::pair<int, std::string>> g_lyrics;  // 时间(ms), 文本
static int g_currentLyricIndex = -1;                       // 当前高亮行索引
static bool g_needScrollToLyric = false;                   // 是否需要滚动到当前歌词

static float g_leftWidthRatio = 0.65f;   // 左侧宽度比例（0.65 表示左侧占 65%）
static bool g_resizingSplitter = false;   // 是否正在拖拽分割条

static int g_PlayOrder = 0;           // 0=顺序播放, 1=单曲循环, 2=随机播放
static std::vector<std::string> g_RemainPlaylist;

// 歌词自动滚动控制
static int g_scrollBlockCounter = 0;   // 0=允许自动滚动，>0=禁止自动滚动（每帧减1）
static int g_scrollBanCounter = 0;     // >0 时禁止自动滚动，每帧递减
static float g_lyricsLastScrollY = 0;  // 上一帧的滚动位置
static bool g_userWheel = false;

static ImVec4 g_accent;

ImFont* g_FontNormal = nullptr;
ImFont* g_FontLarge = nullptr;

static bool g_showSearchWindow = false;           // 是否显示搜索窗口
static char g_searchKeyword[256] = "";            // 搜索关键词
static std::vector<int> g_searchResults;          // 存储匹配项的索引
static int g_currentSearchIndex = -1;             // 当前选中的搜索结果索引

static bool g_needScrollToSong = false;
static int g_targetScrollIndex = -1;

static bool g_AMLL_connect = false;//播放状态

static bool g_sonos_connect = false;//sonos连接状态

//static std::vector<std::string> only_music_file_name = {}; //万年老数组 再见

// 放在合适的作用域（例如 main 循环前，或者作为类的成员）
static std::unique_ptr<SonosController> g_sonos;          // 全局控制器（只初始化一次）
static std::future<std::vector<DeviceEntry>> g_searchFuture;
static bool g_sonos_searching = false;
static std::vector<DeviceEntry> g_devices;                 // 搜索结果
static int g_selectedDevice = -1;                          // 用户选中的设备索引

static std::atomic<bool> g_loading_music{ false };
static AMLLWebSocketClient* amllplayer = nullptr;
static int g_opacity = 128;
static std::string g_dark_mode = "System";
static bool g_enableSnapToEdge = true;

//static std::string g_musicPath = "";

static std::future<void> g_onlinePlayFuture;

UINT g_uTaskbarBtnMsg = 0;

DWORD g_iconColor = 0x222222;

ManageConfig& cfg = ManageConfig::GetInstance();
static std::atomic<int> g_statusId{ 1 };


std::unordered_map<std::string, TextureHandle> g_iconCache;
// 全局或成员变量
//std::unique_ptr<UPnPManager> g_upnp;

// 延迟释放旧的 album art 纹理，避免在渲染线程与异步线程中同时释放引发竞态
static TextureHandle g_pendingReleaseTexture = nullptr;

static int g_animStartWidth = 0, g_animTargetWidth = 0;
static int g_animStartHeight = 0, g_animTargetHeight = 0;
static int g_normalWidth = 700;   // 窗口正常宽度，在窗口创建后赋值
static int g_normalHeight = 230;  // 窗口正常高度

static bool g_online_player = false;
//static std::vector<std::string> g_online_album_urls;
static bool g_skip_vip = false;

static bool g_bRenderSuspended = false;

static bool g_netease_account_login_window_status = false;

std::string g_netease_user_cookie = "";

static bool g_showLoginWindow = false;              // 控制登录窗口显示
static char g_phoneInput[32] = "";                  // 手机号输入
static char g_captchaInput[16] = "";                // 验证码输入
static std::string g_loginStatusMsg;                // 状态提示信息

static std::chrono::steady_clock::time_point g_lastCaptchaTime;
static std::atomic<bool> g_captchaSending{ false };
static const int CAPTCHA_COOLDOWN = 60;  // 冷却秒数

static bool g_cache_clean = true;
static int g_cache_limit_mb = 2048;

static bool g_enable_lyric_translate_split = true;

static std::string g_apiAddr = "";

static bool g_netease_account_window_status = false;

static std::string g_activeThemeZip = "";

static bool g_needUpdateStyle = false;

static TextureHandle g_prevAlbumArt = nullptr;      // 保留上一张图的句柄用于淡出
static float g_albumArtFadeAlpha = 1.0f;            // 0.0 = 完全显示旧图, 1.0 = 完全显示新图
static bool g_isAlbumArtTransitioning = false;      // 是否正在播放动画
static std::chrono::steady_clock::time_point g_albumArtTransStartTime;

static float g_lyricsTargetY = 0.0f;   // 歌词滚动的目标 Y 坐标


struct MusicMetadata {
    std::string title;
    std::string artist;
    std::string album;
    TextureHandle albumArtSRV = nullptr;
};

struct AlbumArtAsyncResult {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgbaData;  // 处理后的 RGBA 数据
    std::vector<unsigned char> rawData;   // 原始封面数据（用于 SMTC）
    std::string title;
    std::string artist;
    std::string album;
    int targetIndex = -1;  // 用于防止快速切换歌曲时的竞态
};

struct AsyncPlayResult {
    int taskId = 0;
    bool success = false;
    std::string localPath;      // 成功时有效
    std::string errorMsg;       // 失败时有效
    int index = -1;             // 歌曲索引
    // 可添加元数据，但可由主线程从文件读取
};

enum StatusCode {
    STATUS_FETCHING_URL = 1,
    STATUS_DOWNLOADING = 2,
    STATUS_WRITING_TAGS = 3,
    STATUS_FETCHING_META = 4,
    STATUS_ERROR = -1
};

std::string u8string_to_string(const std::u8string& u8str) {
    return std::string(reinterpret_cast<const char*>(u8str.data()), u8str.size());
}

void RegisterMusicSources() {
    auto& reg = MusicSourceRegistry::GetInstance();
    reg.Register({ "local",  "本地播放",       CreateLocalSource });
    reg.Register({ "online", "在线[网易云]", CreateNeteaseSource });
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
        OutputDebugStringA("\n-----------没有读取到信息-------------\n\n");
    }
    return meta;
}

MusicMetadata MusicMeta = { "Unknown Title","Unknown Artist","Unknown Album" };

inline void SetStatus(int id) {
    g_statusId.store(id, std::memory_order_relaxed);
}

inline const char* GetStatusText() {
    switch (g_statusId.load(std::memory_order_relaxed)) {
    case STATUS_FETCHING_URL:   return "正在获取播放链接";
    case STATUS_DOWNLOADING:    return "正在下载歌曲";
    case STATUS_WRITING_TAGS:   return "正在写入标签";
    case STATUS_FETCHING_META:  return "正在获取元数据";
    default:                    return "未知";
    }
}

TextureHandle GetIcon(const std::string& name) {
    auto it = g_iconCache.find(name);
    if (it != g_iconCache.end())
        return it->second;

    std::string path;

    // 优先用主题里的图标
    auto* theme = ThemePackManager::GetInstance().GetActive();
    if (theme && !theme->iconsDir.empty()) {
        std::string themed = theme->iconsDir + "/" + name + ".svg";
        if (std::filesystem::exists(std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(themed.data()), themed.size())))) {
            path = themed;
        }
    }

    // 回落到默认资源
    if (path.empty()) {
        path = "resources/" + name + ".svg";
    }

    auto tex = IconManager::GetInstance().LoadIcon(path.c_str(), 16, 16, g_iconColor);
    g_iconCache[name] = tex;
    return tex;
}

//ID3D11Device* g_pd3dDevice = nullptr;
//ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
//IDXGISwapChain* g_pSwapChain = nullptr;
//ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
//HWND g_hWnd = nullptr;


static bool g_isDragging = false;
static POINT g_dragStartPos;
static POINT g_windowStartPos;

static ma_engine g_AudioEngine;
static ma_sound g_CurrentSound;
static bool g_IsSoundLoaded = false;

static std::wstring coverPath;

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


ImVec4 ConvertToImVec4(uint32_t color, bool hasAlpha = false) {
    // 0xAARRGGBB -> (A,R,G,B)
    float a = hasAlpha ? ((color >> 24) & 0xFF) / 255.0f : 1.0f;
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    return ImVec4(r, g, b, a);
}

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
    std::string ext = u8string_to_string(
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

                    coverExt = u8string_to_string(
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

void UpdateImGuiStyle() {
    
    ImGuiStyle& style = ImGui::GetStyle();
    bool isDark = false;
    // 1. 获取当前系统深色模式状态和强调色
    if (g_dark_mode == "System") {
        isDark = IsDarkModeByRegistry();   // 你之前实现的函数
    }
    else if (g_dark_mode == "Dark") {
        isDark = true;
    }
    else {
        isDark = false;
    }
    DWORD accentColor = GetAccentColorFromRegistry();
    ImVec4 accent = ConvertToImVec4(accentColor);
    g_accent = accent;
    // 2. 设置基础色板 (深色/浅色)
    if (auto* t = ThemePackManager::GetInstance().GetActive()) { //主题接管
        DWORD accentColor = GetAccentColorFromRegistry();
        ImVec4 accent = ConvertToImVec4(accentColor);
        ThemePackManager::GetInstance().ApplyToImGui(accent, isDark);
        g_accent = t->accent.useSystem ? accent : t->accent.custom;
        return;
    }

    if (isDark) {
        // 深色主题基础色
        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);
        style.Colors[ImGuiCol_InputTextCursor] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 0.94f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.20f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.30f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.30f, 0.10f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.35f, 0.40f, 0.10f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.08f, 0.80f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 0.30f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 0.40f);
        style.Colors[ImGuiCol_CheckMark] = accent;  // 使用强调色
        style.Colors[ImGuiCol_SliderGrab] = accent;
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.20f, 0.30f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.60f, 0.60f, 0.30f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
        style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.5f, 0.5f, 0.5f, 0.20f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.6f, 0.6f, 0.6f, 0.80f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.40f, 0.40f, 0.80f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_SeparatorActive] = accent;
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.30f, 0.80f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_ResizeGripActive] = accent;
        style.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.12f, 0.86f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(accent.x, accent.y, accent.z, 1.00f);
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.14f, 0.97f);
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
        style.Colors[ImGuiCol_PlotLinesHovered] = accent;
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = accent;
        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
        style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.70f, 0.70f, 0.80f, 0.70f);
        style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
    }
    else {
        // 浅色主题基础色
        style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
        style.Colors[ImGuiCol_InputTextCursor] = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.96f, 0.94f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.20f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.60f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.30f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.85f, 0.85f, 0.87f, 0.10f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.80f, 0.80f, 0.82f, 0.10f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.94f, 0.94f, 0.96f, 0.80f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.94f, 0.94f, 0.96f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.90f, 0.90f, 0.92f, 0.20f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.70f, 0.75f, 0.40f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.65f, 0.40f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.55f, 0.80f);
        style.Colors[ImGuiCol_CheckMark] = accent;
        style.Colors[ImGuiCol_SliderGrab] = accent;
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
        style.Colors[ImGuiCol_Button] = ImVec4(0.90f, 0.90f, 0.90f, 0.30f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.50f, 0.50f, 0.30f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
        style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.5f, 0.5f, 0.5f, 0.60f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.6f, 0.6f, 0.6f, 0.80f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.60f, 0.60f, 0.65f, 0.80f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_SeparatorActive] = accent;
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.70f, 0.70f, 0.75f, 0.80f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_ResizeGripActive] = accent;
        style.Colors[ImGuiCol_Tab] = ImVec4(0.88f, 0.88f, 0.90f, 0.86f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(accent.x, accent.y, accent.z, 1.00f);
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.92f, 0.92f, 0.94f, 0.97f);
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
        style.Colors[ImGuiCol_PlotLinesHovered] = accent;
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = accent;
        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
        style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.70f, 0.70f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.80f, 0.80f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
        style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.50f, 0.50f, 0.55f, 0.70f);
        style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
    }

    // 3. 可选的全局样式调整
    style.WindowRounding = 4.0f;
    style.FrameRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 0.0f;
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


bool SelectableWithStyle(const char* label, bool selected, float rounding) {
    // 禁用所有默认背景（选中、悬浮、按下）
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));

    bool is_clicked = ImGui::Selectable(label, selected);

    ImGui::PopStyleColor(3);

    bool is_hovered = ImGui::IsItemHovered();
    bool is_active = ImGui::IsItemActive();   // 鼠标按下状态

    if (selected || is_hovered || is_active) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();

        // 动态限制圆角半径
        float item_height = max.y - min.y;
        float actual_rounding = (rounding > item_height * 0.5f) ? (item_height * 0.5f) : rounding;

        // 根据状态选择颜色
        ImU32 col;
        if (selected)
            col = ImGui::GetColorU32(ImGuiCol_Header);
        else if (is_active)
            col = ImGui::GetColorU32(ImGuiCol_HeaderActive);
        else
            col = ImGui::GetColorU32(ImGuiCol_HeaderHovered);

        draw_list->AddRectFilled(min, max, col, actual_rounding);
    }

    return is_clicked;
}


//std::wstring s2ws(const std::string& str) {//万年老函数，老资历 ！
//    return Utf8ToPath(str);
//}
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

ma_uint64 GetAudioFileDurationMs(const std::string& utf8Path) {
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

    return (ma_uint64)(seconds * 1000.0);
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


// 准备在线歌曲：下载、缓存、写入元数据，返回本地路径，失败返回空字符串
void PlayLocalFile(const std::string& filepath_utf8) {
    PathType widePath = Utf8ToPath(filepath_utf8);
    if (widePath.empty()) return;

    // 保留旧封面句柄，重置元数据
    TextureHandle oldArt = MusicMeta.albumArtSRV;
    MusicMeta = GetMusicMetadata(filepath_utf8);
    MusicMeta.albumArtSRV = oldArt;

    // ==========================================
    // 1. 播放音频引擎 (MiniAudio 或 Sonos)
    // ==========================================
    if (!g_sonos_connect) {
        // 本地播放
        if (MA_SOUND_INIT_FROM_FILE(&g_AudioEngine, widePath.c_str(), MA_SOUND_FLAG_STREAM, NULL, NULL, &g_CurrentSound) != MA_SUCCESS) {
            OutputDebugStringA(("播放失败: " + filepath_utf8 + "\n").c_str());
            return;
        }
        IsPlaying = true;//迟来的代码
    }
    else {
        // Sonos 播放：提取纯文件名，丢给 HTTP 服务器
        std::string baseName = filepath_utf8.substr(filepath_utf8.find_last_of("/\\") + 1);
        g_sonos->PlayFile(baseName);
        IsPlaying = true;
    }

    // ==========================================
    // 2. 获取时长
    // ==========================================
    float totalSec = 0.0f;
    if (!g_sonos_connect) {
        if (ma_sound_get_length_in_seconds(&g_CurrentSound, &totalSec) == MA_SUCCESS) {
            PlayMusicLong = totalSec;
            PlayMusicLongMs = (ma_uint64)(totalSec * 1000.0);
        }
        else {
            PlayMusicLong = 0.0f;
            PlayMusicLongMs = 0;
        }
        if (ma_sound_get_length_in_pcm_frames(&g_CurrentSound, &g_TotalPCMFrames) != MA_SUCCESS) {
            g_TotalPCMFrames = 0;
        }
    }
    else {
        g_sonos->GetPlayer()->SetPlayMode(SONOS::PlayMode_NORMAL);
        PlayMusicLongMs = GetAudioFileDurationMs(filepath_utf8);
    }

    // ==========================================
    // 3. 解析歌词
    // ==========================================
    g_lyrics.clear();
    g_currentLyricIndex = -1;
    g_needScrollToLyric = false;
    std::string orginal_lrc = "Unknow";

    std::string id3Lyrics = GetLyricsFromFile(widePath);
    if (!id3Lyrics.empty()) {
        g_lyrics = ParseLrc(id3Lyrics);
        orginal_lrc = id3Lyrics;
    }
    else {
        PathType lrcPath = FindLrcFile(widePath);
        if (!lrcPath.empty()) {
            std::string content = ReadTextFile(lrcPath);
            if (!content.empty()) {
                if (content.size() >= 3 && (unsigned char)content[0] == 0xEF
                    && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
                    content = content.substr(3);
                }
                g_lyrics = ParseLrc(content);
                orginal_lrc = content;
            }
        }
    }

    // ==========================================
    // 4. 提取封面数据并处理 SMTC
    // ==========================================
    std::vector<unsigned char> rawAlbumData = ExtractAlbumArt(widePath);
    int currentTaskId = ++g_AsyncTaskId_search;

    std::vector<unsigned char> amllCoverData;
    if (!rawAlbumData.empty()) {
        amllCoverData = ConvertImageToJPEG(rawAlbumData);
        if (amllCoverData.empty()) {
            amllCoverData = rawAlbumData;
        }

        std::vector<unsigned char> jpegData = ConvertImageToJPEG(rawAlbumData);
        if (!jpegData.empty()) {
            UpdateSMTC(MusicMeta.title, MusicMeta.artist, MusicMeta.album, jpegData, PlayMusicLongMs);
        }
        else {
            UpdateSMTC(MusicMeta.title, MusicMeta.artist, MusicMeta.album, rawAlbumData, PlayMusicLongMs);
        }
    }
    else {
        UpdateSMTC(MusicMeta.title, MusicMeta.artist, MusicMeta.album, {}, PlayMusicLongMs);
    }

    // ==========================================
    // 5. 异步处理 UI 封面 (带淡入淡出)
    // ==========================================
    if (!rawAlbumData.empty()) {
        OutputDebugStringA(("[PlayLocalFile] raw album art size=" + std::to_string(rawAlbumData.size()) + "\n").c_str());
        std::string title = MusicMeta.title;
        std::string artist = MusicMeta.artist;
        std::string album = MusicMeta.album;
        std::vector<unsigned char> rawDataCopy = rawAlbumData;

        std::thread([rawDataCopy, title, artist, album, currentTaskId]() {
            if (currentTaskId != g_AsyncTaskId_search) return;

            std::vector<unsigned char> processedForUI = ProcessAlbumArtWithGradient(rawDataCopy, 230, 230);
            if (processedForUI.empty()) return;

            if (currentTaskId != g_AsyncTaskId_search) return;

            AlbumArtAsyncResult* result = new AlbumArtAsyncResult();
            result->width = 230;
            result->height = 230;
            result->rgbaData = std::move(processedForUI);
            result->rawData = rawDataCopy;
            result->title = title;
            result->artist = artist;
            result->album = album;
            result->targetIndex = currentTaskId;
            PostMessage(g_hWnd, WM_UPDATE_ALBUM_ART, 0, reinterpret_cast<LPARAM>(result));
            }).detach();
    }
    else {
        AlbumArtAsyncResult* emptyResult = new AlbumArtAsyncResult();
        emptyResult->targetIndex = currentTaskId;
        PostMessage(g_hWnd, WM_UPDATE_ALBUM_ART, 0, reinterpret_cast<LPARAM>(emptyResult));
    }

    // ==========================================
    // 6. 启动播放并发送 AMLL 消息
    // ==========================================
    SetSMTCPlaybackStatus(PlaybackStatus::Playing);

    if (!g_sonos_connect) {
        ma_sound_start(&g_CurrentSound);
        g_IsSoundLoaded = true;
    }

    std::string id1 = "t0";
    std::string id2 = "al0";
    std::string id3 = "ar0";
    std::vector<Artist> artists = { {id3, MusicMeta.artist} };
    if (amllplayer) {
        amllplayer->sendMusicInfo(id1, MusicMeta.title, id2, MusicMeta.album, artists, PlayMusicLongMs);
        if (!amllCoverData.empty()) {
            std::string jpegUriData = amllplayer->imageToDataURI(amllCoverData, "image/jpeg");
            amllplayer->sendAlbumCoverByURI(jpegUriData);
        }
        amllplayer->sendLyricFromLRC(orginal_lrc, g_enable_lyric_translate_split);
    }
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

    if (g_IsSoundLoaded) {
        ma_sound_stop(&g_CurrentSound);
        ma_sound_uninit(&g_CurrentSound);
        g_IsSoundLoaded = false;
    }
    g_scrollBlockCounter = 0;

    CurrentIndex = index;
    g_source->SaveIndex(index);

    int currentTaskId = ++g_AsyncTaskId_download;

    std::thread([index, currentTaskId]() {
        // 1. 获取信息
        PlaybackInfo info = g_source->PreparePlayback(index);

        if (currentTaskId != g_AsyncTaskId_download) return;

        if (!info.isValid) {
            AsyncPlayResult errorRes;
            errorRes.taskId = currentTaskId;
            errorRes.success = false;
            errorRes.errorMsg = "获取播放信息失败";
            PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(errorRes));
            return;
        }

        // 2. 统一下载（本地直接返回路径，在线下载+打标签）
        std::string finalLocalPath = DownloadAndProcessTrack(info);

        if (currentTaskId != g_AsyncTaskId_download) return;

        // 3. 通知主线程播放
        AsyncPlayResult result;
        result.taskId = currentTaskId;
        result.index = index;
        if (finalLocalPath.empty()) {
            result.success = false;
            result.errorMsg = "下载或处理失败";
            PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(result));
        }
        else {
            result.success = true;
            result.localPath = finalLocalPath;
            PostMessage(g_hWnd, WM_PLAY_READY, 0, (LPARAM)new AsyncPlayResult(result));
        }
        }).detach();
}

//void PlayMusicFile(const std::string& filepath_utf8) {
//    g_loading_music = true;
//    SetStatus(STATUS_FETCHING_URL);
//
//    if (g_IsSoundLoaded) {
//        ma_sound_stop(&g_CurrentSound);
//        ma_sound_uninit(&g_CurrentSound);
//        g_IsSoundLoaded = false;
//    }
//    g_scrollBlockCounter = 0;
//
//    if (!g_online_player) {
//        PlayLocalFile(filepath_utf8);
//        g_loading_music = false;
//        return;
//    }
//
//    int currentTaskId = ++g_AsyncTaskId_download;
//    int idx = CurrentIndex;
//    std::string cookieCopy = g_netease_user_cookie;
//
//    // ★ 终极修复：改用 std::thread 彻底分离，主线程绝不等待！
//    std::thread([filepath_utf8, currentTaskId, idx, cookieCopy]() {
//        AsyncPlayResult result;
//        result.taskId = currentTaskId;
//        result.index = idx;
//        result.success = false;
//        result.localPath = "";
//
//        try {
//            // 3.1 获取播放 URL
//            std::string playUrl = NeteaseAPIManager::GetInstance().GetSongUrl(filepath_utf8, "exhigh", cookieCopy);
//
//            // ★ 如果在请求 URL 的时候用户已经切歌，直接放弃，别下载了
//            if (currentTaskId != g_AsyncTaskId_download) return;
//
//            if (playUrl.empty()) {
//                result.errorMsg = "获取播放 URL 失败";
//                PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(result));
//                return;
//            }
//            std::string extension = ".mp3";
//            if (ExtractExtensionFromUrl(playUrl) == ".flac") {
//                extension = ".flac";
//            }
//
//            // 3.2 缓存路径
//            SetStatus(STATUS_DOWNLOADING);
//            std::string cacheDir = GetCacheDirectory();
//            std::string localPath = cacheDir + filepath_utf8 + extension;
//            std::string imagePath = cacheDir + "temp_cover_" + filepath_utf8 + ".jpg";
//
//            // 3.3 如果已缓存且有效，直接使用
//            if (std::filesystem::exists(localPath) && std::filesystem::file_size(localPath) > 0) {
//                OutputDebugStringA(("使用缓存: " + localPath + "\n").c_str());
//                result.localPath = localPath;
//                result.success = true;
//                PostMessage(g_hWnd, WM_PLAY_READY, 0, (LPARAM)new AsyncPlayResult(result));
//                return;
//            }
//
//            // 3.4 下载 MP3
//            if (!DownloadFile(playUrl, localPath)) {
//                result.errorMsg = "下载 MP3 失败";
//                PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(result));
//                return;
//            }
//
//            // ★ 下载完歌曲后，再检查一次是否被切歌
//            if (currentTaskId != g_AsyncTaskId_download) return;
//
//            // 3.5 下载封面
//            std::string coverUrl = g_online_album_urls[idx];
//            if (!coverUrl.empty()) {
//                if (!DownloadFile(coverUrl, imagePath)) {
//                    OutputDebugStringA(("下载封面失败: " + coverUrl + "\n").c_str());
//                }
//            }
//            if (g_cache_clean) {
//                EnforceCacheLimit(g_cache_limit_mb);
//            }
//            SetStatus(STATUS_FETCHING_META);
//
//            // 3.6 获取歌词
//            std::string lyrics = NeteaseAPIManager::GetInstance().GetLyrics(filepath_utf8);
//
//            // ★ 获取歌词后，再检查一次
//            if (currentTaskId != g_AsyncTaskId_download) return;
//
//            // 3.7 获取歌曲完整信息
//            NeteaseSongInfo info = NeteaseAPIManager::GetInstance().GetSongFullInfo(filepath_utf8);
//
//            SetStatus(STATUS_WRITING_TAGS);
//
//            // 3.8 写入元数据
//            if (std::filesystem::exists(imagePath)) {
//                WriteMetadataToFile(localPath, info, imagePath, lyrics);
//                std::filesystem::remove(imagePath);
//            }
//            else {
//                WriteMetadataToFile(localPath, info, "", lyrics);
//            }
//
//            result.localPath = localPath;
//            result.success = true;
//            PostMessage(g_hWnd, WM_PLAY_READY, 0, (LPARAM)new AsyncPlayResult(result));
//
//        }
//        catch (const std::exception& e) {
//            result.errorMsg = e.what();
//            PostMessage(g_hWnd, WM_PLAY_FAILED, 0, (LPARAM)new AsyncPlayResult(result));
//        }
//        }).detach(); // ★ 彻底分离，瞬间返回
//
//    OutputDebugStringA(("正在播放: " + filepath_utf8 + "\n").c_str());
//}


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

    // ── 跨平台清理 ──
    if (g_IsSoundLoaded) {
        ma_sound_uninit(&g_CurrentSound);
    }
    if (MusicMeta.albumArtSRV) {
		ReleaseTexture(MusicMeta.albumArtSRV);
        MusicMeta.albumArtSRV = nullptr;
    }
    StopWindowTimer(hWnd, g_animTimer);
    std::filesystem::remove(coverPath);          // ← 替代 DeleteFileW
    ma_engine_uninit(&g_AudioEngine);
    CleanupNetwork();                            // ← 见下方

    // ── 平台清理 ──
    UnregisterSessionNotification();
    UnregisterAllHotkeys();

    RequestQuitProgram();

    return 0;
}
bool ChangePlayMode() {
    if (IsPlaying == false) {
        if (!g_sonos_connect) {
            ma_sound_start(&g_CurrentSound);
        }
        else {
            g_sonos->GetPlayer()->Play();
        }
        IsPlaying = true;
        if (amllplayer) {
            amllplayer->sendResumed();
        }
    }
    else {
        if (!g_sonos_connect) {
            ma_sound_stop(&g_CurrentSound);
        }
        else {
            g_sonos->GetPlayer()->Pause();
        }
        IsPlaying = false;
        if (amllplayer) {
            amllplayer->sendPaused();
        }
    }
    SetSMTCPlaybackStatus(IsPlaying ? PlaybackStatus::Playing : PlaybackStatus::Paused);

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
    if (result->taskId != g_AsyncTaskId_download) {
        delete result;
        return;
    }

    if (result->success) {
        // ★ 主线程更新 UI 的当前索引
        CurrentIndex = result->index;
        // ★ 底层只负责放音
        PlayLocalFile(result->localPath);
    }
    else {
        OutputDebugStringA(("播放失败: " + result->errorMsg + "\n").c_str());
    }

    delete result;
    g_loading_music = false;
}
void OnPlayFailed(AsyncPlayResult* result) {
    OutputDebugStringA(("准备失败: " + result->errorMsg + "\n").c_str());
    g_loading_music = false;
    delete result;
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

    // 只有最新任务才处理
    if (result->targetIndex == g_AsyncTaskId_search) {
        // 1. 把当前正在显示的图移交给“上一张”
        if (g_prevAlbumArt) {
            ReleaseTexture(g_prevAlbumArt); // 如果还有更早的，先释放
            g_prevAlbumArt = nullptr;
        }
        if (MusicMeta.albumArtSRV) {
            g_prevAlbumArt = MusicMeta.albumArtSRV;
            MusicMeta.albumArtSRV = nullptr;
        }

        // 2. 创建新图 (如果 rgbaData 为空，说明是纯淡出，新图就是 nullptr)
        if (!result->rgbaData.empty()) {
            MusicMeta.albumArtSRV = CreateTextureFromRGBA(result->rgbaData, result->width, result->height);
        }

        // 3. 启动交叉淡入淡出动画
        g_albumArtFadeAlpha = 0.0f;
        g_isAlbumArtTransitioning = true;
        g_albumArtTransStartTime = std::chrono::steady_clock::now(); // ★ 新增：记录时间点
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
void OnSonosStopped() {
    if (g_sonos_connect && IsPlaying) {
        AutoPlayNextSong();
    }
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

    case WM_SONOS_STOPPED:
        OnSonosStopped();
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
    g_userWheel = false;
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
        ApplyTheme(g_dark_mode, g_opacity);
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
        g_userWheel = true;
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

std::string FormatTime(ma_uint64 milliseconds) {//转换时间为mm:ss的辅助函数
    int totalSeconds = (int)(milliseconds / 1000);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    return std::string(buf);
}

bool seek(float seekRatio) {
    seekRatio = std::clamp(seekRatio, 0.0f, 1.0f);

    // 执行 Seek（使用毫秒）
    if (!g_sonos_connect) {
        if (g_IsSoundLoaded && g_TotalPCMFrames > 0) {
            ma_uint64 targetFrame = (ma_uint64)(seekRatio * g_TotalPCMFrames);
            PlayCurrentMs = PlayMusicLongMs * seekRatio;
            if (ma_sound_seek_to_pcm_frame(&g_CurrentSound, targetFrame) != MA_SUCCESS) {
                OutputDebugStringA("Seek to PCM Frame Failed!\n");
                return false;
            }
            return true;
        }
    }
    else {
        g_sonos->SetPlayPos(PlayMusicLongMs * seekRatio);
        return true;
    }
}

void ConnectAMLL() {
    if (amllplayer) return; // 已经连接
    // 使用你自己的 WebSocket 地址（可从配置读取或硬编码）
    std::string address = cfg.GetValue<std::string>("amll_address", "ws://localhost:11444");
    amllplayer = new AMLLWebSocketClient(address);
    amllplayer->start();
    OutputDebugStringA("AMLL 连接已启动\n");
}

void DisconnectAMLL() {
    if (!amllplayer) return;
    amllplayer->stop();      // 内部会停止线程并关闭 WebSocket
    delete amllplayer;
    amllplayer = nullptr;
    OutputDebugStringA("AMLL 连接已关闭\n");
}


bool InitPlayerEngine() {
    if (ma_engine_init(NULL, &g_AudioEngine) != MA_SUCCESS) {
        OutputDebugStringA("音频引擎初始化失败\n");
        ShowMessageBox("错误", "音频引擎初始化失败");
        return false;
    }
    return true;
}

bool InitWindow(const std::string& className) {
    // 1. 建窗口（平台）
    if (!InitWindowPlat(className)) return false;

    // 2. 业务初始化
    UpdateWindowSide();
    g_currentAlpha = 255;
    g_isWindowHidden = false;
    g_NeedScrollToCurrent = true;

    // 3. 恢复窗口位置
    int lastX = cfg.GetValue<int>("window_x", -1);
    int lastY = cfg.GetValue<int>("window_y", -1);
    if (lastX != -1 && lastY != -1) {
        RECT workArea = GetWorkAreaPlat();      // ← 需要抽这个
        RECT winRect = GetWindowRectPlat(g_hWnd);
        int winWidth = winRect.right - winRect.left;
        int winHeight = winRect.bottom - winRect.top;
        lastX = (std::max)((int)workArea.left, (std::min)(lastX, (int)(workArea.right - winWidth)));
        lastY = (std::max)((int)workArea.top, (std::min)(lastY, (int)(workArea.bottom - winHeight)));
        MoveWindowPlat(g_hWnd, lastX, lastY);
    }

    // 4. 应用主题
    UpdateWindowSide();
    ApplyTheme(g_dark_mode, g_opacity);
    SetWindowAlpha(g_hWnd, 255);

    // 5. 创建 D3D
    if (!CreateDeviceD3D(g_hWnd)) {
        ShowMessageBox("错误", "DX11 初始化失败");
        return false;
    }
    return true;
}

void LoadTheme() {
    if (!g_activeThemeZip.empty()) {
        // 绝对路径 zip
        std::string id = ThemePackManager::GetInstance().LoadFromZipAbsolute(g_activeThemeZip);
        if (id.empty()) {
            OutputDebugStringA(("加载主题包失败: " + g_activeThemeZip + "\n").c_str());
        }
    }
    ThemePackManager::GetInstance().LoadActiveBackgroundTexture();
}

void ReadConfig() {
    cfg.Load();
    auto playmode = cfg.GetValue<std::string>("play_mode", "local");
    if (playmode=="local") {
        g_online_player = false;
    }
    else if (playmode == "online") {
        g_online_player = true;
    }
    g_apiAddr = cfg.GetValue<std::string>("netease_api_address", "");
    g_skip_vip = cfg.GetValue<bool>("skip_vip", false);
    g_cache_clean = cfg.GetValue<bool>("enable_cache_limit", true);
    g_cache_limit_mb = cfg.GetValue<int>("cache_limit_mb", 2048);
    g_enable_lyric_translate_split = cfg.GetValue<bool>("enable_lyric_translate_split", true);
    g_opacity = cfg.GetValue("acrylic_opacity", 128);
    if (g_opacity < 0 || g_opacity>255) {
        g_opacity = 128; // 默认值
    }
    g_dark_mode = cfg.GetValue<std::string>("theme_style", "System");
    g_enableSnapToEdge = cfg.GetValue<bool>("enable_snap_to_edge", true);
    g_activeThemeZip = cfg.GetValue<std::string>("active_theme_zip", "");
}

void InitUserCookie() {
    //初始化用户cookie

    //调试区域：从明文文件载入cookie

    //std::ifstream infile("D:/cookie.txt");
    //if (infile.is_open()) {
    //    // 处理 cookie 文件
    //    std::getline(infile, g_netease_user_cookie);
    //    infile.close();
    //}
    std::string base64_encryptedCookie = cfg.GetValue<std::string>("encrypted_cookie", "");
    if (base64_encryptedCookie.empty()) {
        NeteaseAPIManager::GetInstance().LoginAsGuest();
    }
    std::string encryptedCookie2;
    if (!base64_encryptedCookie.empty()) {
        encryptedCookie2 = Base64Decode(base64_encryptedCookie);
    }
    g_netease_user_cookie.clear();

    if (!encryptedCookie2.empty()) {
        DecryptString(encryptedCookie2, g_netease_user_cookie);
    }
    NeteaseAPIManager::GetInstance().SetUserCookie(g_netease_user_cookie);
    long long last_update_cookie = 0;
    std::time_t now = std::time(nullptr);
    last_update_cookie = cfg.GetValue("last_update_cookie", last_update_cookie);
    if (now - last_update_cookie > 24 * 60 * 60) {
        // 如果超过1天没有更新cookie，尝试刷新
        if (!g_netease_user_cookie.empty()) {
            if (NeteaseAPIManager::GetInstance().UpdateCookie(g_netease_user_cookie)) {
                cfg.SetValue("last_update_cookie", now);
                cfg.Save();
                g_netease_user_cookie = NeteaseAPIManager::GetInstance().GetCurrentCookie();
                std::string encryptedCookie;
                if (EncryptString(g_netease_user_cookie, encryptedCookie)) {
                    std::string base64_encryptedCookie = Base64Encode(encryptedCookie);
                    cfg.SetValue("encrypted_cookie", base64_encryptedCookie);
                    cfg.Save();
                }
            }
            else {
                OutputDebugStringA("更新cookie失败，可能需要重新登录\n");
            }
        }
    }
}


void RenderFrame(int counter,SettingWindow& settings) {
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Main", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus
    );



    // 获取主窗口大小
    ImVec2 winSize = ImGui::GetWindowSize();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    // 如果存在封面纹理，绘制在窗口右侧（230x230）
#if PLATFORM_LINUX
    { //linux需要背景 不然太透明了
        bool isDark = ResolveIsDark(g_dark_mode);

        // 深色：黑灰；浅色：白灰
        float bgR = isDark ? 0.10f : 0.96f;
        float bgG = isDark ? 0.10f : 0.96f;
        float bgB = isDark ? 0.12f : 0.98f;

        // 透明度：g_opacity [0, 255]  alpha [0.0, 1.0]
        float alpha = g_opacity / 255.0f;
        if (alpha < 0.00f) alpha = 0.00f;   // 下限，避免完全看不见
        if (alpha > 1.00f) alpha = 1.00f;

        ImU32 bgColor = ImGui::GetColorU32(ImVec4(bgR, bgG, bgB, alpha));

        // 铺满整个窗口，圆角 0px
        ImVec2 pMin = winPos;
        ImVec2 pMax = ImVec2(winPos.x + winSize.x, winPos.y + winSize.y);
        float rounding = 0.0f;

        draw_list->AddRectFilled(pMin, pMax, bgColor, rounding);

        // 可选：细边框（深浅色适配）
        float borderAlpha = alpha * 0.6f;
        ImU32 borderColor = ImGui::GetColorU32(
            isDark ? ImVec4(1.0f, 1.0f, 1.0f, borderAlpha * 0.15f)
                   : ImVec4(0.0f, 0.0f, 0.0f, borderAlpha * 0.20f));
        draw_list->AddRect(pMin, pMax, borderColor, rounding, 0, 1.0f);
    }
#endif
    // 主题背景
    if (auto* theme = ThemePackManager::GetInstance().GetActive()) {
        if (theme->backgroundTexture) {
            draw_list->AddImage(
                (ImTextureID)theme->backgroundTexture,
                winPos,
                ImVec2(winPos.x + winSize.x, winPos.y + winSize.y)
            );
        }
    }
        // ================= 处理专辑图淡入淡出动画 =================
        // ================= 处理专辑图淡入淡出动画 =================
    if (g_isAlbumArtTransitioning) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - g_albumArtTransStartTime).count();

        // 0.35 秒完成过渡，再也不怕卡顿跳帧
        g_albumArtFadeAlpha = elapsed / 0.35f;

        if (g_albumArtFadeAlpha >= 1.0f) {
            g_albumArtFadeAlpha = 1.0f;
            g_isAlbumArtTransitioning = false;
            // 动画结束，彻底释放旧图
            if (g_prevAlbumArt) {
                ReleaseTexture(g_prevAlbumArt);
                g_prevAlbumArt = nullptr;
            }
        }
    }

    float imgWidth = 230.0f;
    float imgHeight = 230.0f;
    ImVec2 imgPos(winPos.x + winSize.x - imgWidth, winPos.y);
    ImVec2 imgEndPos(imgPos.x + imgWidth, imgPos.y + imgHeight);

    // 1. 绘制旧图（透明度从 1.0 渐变到 0.0）
    if (g_prevAlbumArt) {
        float alpha = 1.0f - g_albumArtFadeAlpha;
        ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
        draw_list->AddImage((ImTextureID)g_prevAlbumArt, imgPos, imgEndPos, ImVec2(0,0), ImVec2(1,1), tint);
    }

    // 2. 绘制新图（透明度从 0.0 渐变到 1.0）
    if (MusicMeta.albumArtSRV) {
        float alpha = g_albumArtFadeAlpha;
        ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
        draw_list->AddImage((ImTextureID)MusicMeta.albumArtSRV, imgPos, imgEndPos, ImVec2(0,0), ImVec2(1,1), tint);
    }
    // ==========================================================


    // 左侧区域宽度
    float leftWidth = winSize.x * g_leftWidthRatio;
    float splitterWidth = 6.0f;  // 分割条宽度
    float rightWidth = winSize.x - leftWidth - splitterWidth - (8.0f) * 4;

    // 左侧区域（歌曲列表、按钮、进度条）
    ImGui::BeginChild("LeftPane", ImVec2(leftWidth, 0), true);
    {
        ImGui::BeginChild("MainLayout", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);


        // 设置列表大小（宽度自适应，高度200像素）
        float available_height = ImGui::GetContentRegionAvail().y;
        float progress_height = 24.0f;
        float button_bar_height = ImGui::GetFrameHeightWithSpacing(); // 大约
        float listbox_height = available_height - progress_height - button_bar_height - 8.0f;
        ImVec2 listbox_size(-FLT_MIN, listbox_height);

        ImGui::SetNextItemAllowOverlap();
        if (ImGui::BeginListBox("##MusicList", listbox_size)) {
            bool scrolled = false;
            size_t trackCount = g_source->GetCount();
            for (int i = 0; i < trackCount; ++i) {
                int t_scrollToSong = -1;
                const BaseTrackInfo* track = g_source->GetBaseTrack(i);
                bool is_selected = (CurrentIndex == i);

                if (g_needScrollToSong && i == g_targetScrollIndex) {
                    ImGui::SetScrollHereY(0.5f);
                    g_needScrollToSong = false;
                    t_scrollToSong = g_targetScrollIndex;
                    g_targetScrollIndex = -1;
                }

                // 自动滚动（只滚一次）
                if (g_NeedScrollToCurrent && !scrolled && i == CurrentIndex) {
                    ImGui::SetScrollHereY(0.5f);
                    g_NeedScrollToCurrent = false;
                    scrolled = true;
                }

                // 搜索匹配判断
                bool isMatch = false;
                if (strlen(g_searchKeyword) > 0) {
                    std::string kw = g_searchKeyword;
                    // ★ 修改：从接口获取 displayName 替代旧的 only_music_file_name[i]
                    std::string title = track ? track->displayName : "";

                    std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
                    std::transform(title.begin(), title.end(), title.begin(), ::tolower);

                    if (title.find(kw) != std::string::npos) {
                        isMatch = true;
                    }
                }

                // ======================
                // 颜色样式栈（核心修复）
                // ======================
                int popCount = 0;

                // 1. 当前播放项 → 黄色（最高优先级）
                if (i == t_scrollToSong) {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255)); // 亮黄
                    popCount++;
                }
                // 2. 搜索匹配 → 橙色
                else if (isMatch) {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 140, 0, 255));
                    popCount++;
                }

                // 绘制条目
                if (SelectableWithStyle(track ? track->displayName.c_str() : "No Display Name 无显示名称", is_selected, 6.0f)) {
                    CurrentIndex = i;
                    PlayMusicFile(CurrentIndex);
                }

                // 弹出颜色（必须平衡！）
                if (popCount > 0)
                    ImGui::PopStyleColor(popCount);
            }
            ImGui::EndListBox();
        }
        if (!g_sonos_connect) {
            ma_uint64 currentMs = ma_sound_get_time_in_milliseconds(&g_CurrentSound);
            PlayCurrentMs = currentMs;
            if (PlayMusicLongMs > 0) {
                PlayProgressPercent = (float)PlayCurrentMs / (float)PlayMusicLongMs;
                if (PlayProgressPercent > 1.0f) PlayProgressPercent = 1.0f;
            }
            else {
                PlayProgressPercent = 0.0f;
            }
        }
        else {
            if ((counter % 10 == 0) && g_loading_music == false && IsPlaying == true) {
                PlayCurrentMs = g_sonos->GetPosition();
                PlayProgressPercent = (float)PlayCurrentMs / (float)PlayMusicLongMs;
                if (PlayProgressPercent > 1.0f) PlayProgressPercent = 1.0f;
                if (PlayProgressPercent < 0.0f) PlayProgressPercent = 0.0f;
                if (PlayCurrentMs > PlayMusicLongMs * 1.5) { PlayCurrentMs = 0; }
            }
            else if (g_loading_music) {
                PlayCurrentMs = 0;
            }
        }
        //if (g_pTaskbarList && g_hWnd && counter == 0) {
        //    if (PlayCurrentMs > 0) {
        //        g_pTaskbarList->SetProgressValue(g_hWnd, PlayCurrentMs, PlayMusicLongMs);
        //    }
        //    if (!IsPlaying) {
        //        g_pTaskbarList->SetProgressState(g_hWnd, TBPF_PAUSED);
        //    }
        //    else {
        //        g_pTaskbarList->SetProgressState(g_hWnd, TBPF_NORMAL);
        //    }
        //}

        // ========== 按钮栏（水平排列） =========
        int button_count = 6;
        float availWidth = ImGui::GetContentRegionAvail().x - (8.0f * (button_count - 1));

        if (ImGui::ImageButton("prev_btn", (ImTextureID)GetIcon("prev"), ImVec2(16, 16))) {
            OnMediaPrev();
        }
        
        ImGui::SameLine();
        if (IsPlaying) {
            // 第一个参数是字符串 ID（必须唯一），第二个参数是纹理 ID，第三个是尺寸
            if (ImGui::ImageButton("pause_btn", (ImTextureID)GetIcon("pause"), ImVec2(16, 16))) {
                ChangePlayMode();
            }
        }
        else {
            if (ImGui::ImageButton("play_btn", (ImTextureID)GetIcon("play"), ImVec2(16, 16))) {
                ChangePlayMode();
            }
        }
        ImGui::SameLine();
        if (ImGui::ImageButton("next_btn", (ImTextureID)GetIcon("next"), ImVec2(16, 16))) {
            OnMediaNext();
        }

        ImGui::SameLine();
        // 隐藏/显示窗口按钮
        if (ImGui::ImageButton("hide_btn", (ImTextureID)GetIcon("hide"), ImVec2(16, 16))) {
            StartHideShowAnimation(!g_isWindowHidden);
        }
        ImGui::SameLine();

        // 滚动到当前播放按钮
        if (ImGui::ImageButton("locate_btn", (ImTextureID)GetIcon("locate"), ImVec2(16, 16))) {
            g_NeedScrollToCurrent = true;
        }
        ImGui::SameLine();

        // 播放模式按钮（文字随模式变化）
        const char* modeName = "";
        if (g_PlayOrder == 0) modeName = "order";
        else if (g_PlayOrder == 1) modeName = "round";   // 单曲循环
        else modeName = "random";

        if (ImGui::ImageButton(modeName, (ImTextureID)GetIcon(modeName), ImVec2(16, 16))) {
            g_PlayOrder = (g_PlayOrder + 1) % 3;

            // ★ 切换为随机播放时，重新填充随机播放池
            if (g_PlayOrder == 2) {
                g_RemainPlaylist.clear();
                size_t trackCount = g_source->GetCount();
                for (size_t i = 0; i < trackCount; ++i) {
                    const BaseTrackInfo* track = g_source->GetBaseTrack(i);
                    if (track) {
                        g_RemainPlaylist.push_back(track->id);
                    }
                }
            }
        }
        ImGui::SameLine();

        // 搜索按钮
        if (ImGui::ImageButton("search_btn", (ImTextureID)GetIcon("search"), ImVec2(16, 16))) {
            g_showSearchWindow = !g_showSearchWindow;
        }
        ImGui::SameLine();
        if (ImGui::ImageButton("more_btn", (ImTextureID)GetIcon("more"), ImVec2(16, 16))) {
            ImGui::OpenPopup("更多选项");
        }
        static bool s_open_sonos_search_window = false;
        if (ImGui::BeginPopup("更多选项")) {
            // 这里可以放置任意控件
            ImGui::Text("额外设置");
            ImGui::Separator();

            if (ImGui::MenuItem("打开AMLL连接", NULL, g_AMLL_connect)) {
                g_AMLL_connect = !g_AMLL_connect;
                if (g_AMLL_connect == true) {
                    ConnectAMLL();
                }
                else {
                    DisconnectAMLL();
                }

            }if (ImGui::MenuItem("打开Sonos连接窗口", NULL)) {
                s_open_sonos_search_window = !s_open_sonos_search_window;
            }

            if (g_netease_user_cookie.empty()) {
                if (ImGui::MenuItem("登录网易云音乐账号", NULL)) {
                    g_netease_account_login_window_status = !g_netease_account_login_window_status;
                }
            }
            else {
                if (ImGui::MenuItem("已登录网易云账号", NULL)) {
                    g_netease_account_window_status = !g_netease_account_window_status;
                }
            }

            if (ImGui::MenuItem("设置", NULL)) {
                settings.setOpen(true);
            }
            ImGui::Separator();
            ImGui::EndPopup();
        }
        settings.setSource(g_source.get());
        settings.render();
        if (s_open_sonos_search_window) {
            ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Sonos 控制", &s_open_sonos_search_window, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {

                // 初始化控制器（只做一次）


                ImGui::Text("播放设备控制");
                ImGui::Separator();

                // ---------- 搜索按钮 ----------
                if (ImGui::Button("搜索设备") && !g_sonos_searching) {
                    g_devices.clear();
                    g_selectedDevice = -1;
                    g_sonos_connect = false;

                    // 启动异步搜索
                    g_searchFuture = std::async(std::launch::async, []() {
                        SonosController ctrl;   // 每个线程独立控制器，避免共享状态问题
                        ctrl.Initialize();
                        return ctrl.SearchDevices();
                        });
                    g_sonos_searching = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("回到本机播放") && !g_sonos_searching) {
                    g_selectedDevice = -1;
                    g_sonos_connect = false;
                }

                // ---------- 处理搜索结果 ----------
                if (g_sonos_searching) {
                    // 非阻塞检查是否完成
                    if (g_searchFuture.valid() &&
                        g_searchFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                        g_devices = g_searchFuture.get();
                        g_sonos_searching = false;
                    }
                    else {
                        ImGui::Text("正在搜索... (请稍候)");
                    }
                }

                // ---------- 设备列表 ----------
                if (!g_devices.empty()) {
                    ImGui::Text("发现 %d 个设备:", (int)(g_devices.size()));
                    ImGui::BeginChild("DeviceList", ImVec2(0, 60), true);
                    for (size_t i = 0; i < g_devices.size(); i++) {
                        if (ImGui::Selectable(g_devices[i].friendlyName.c_str(), g_selectedDevice == i)) {
                            g_selectedDevice = (int)i;
                        }
                    }
                    ImGui::EndChild();

                    // ---------- 连接按钮 ----------
                    if (g_selectedDevice >= 0 && !g_sonos_connect) {
                        if (ImGui::Button("连接此设备")) {
                            if (g_sonos->ConnectToDevice(g_devices[g_selectedDevice].location)) {
                                g_sonos_connect = true;
                                ImGui::TextColored(ImVec4(0, 1, 0, 1), "连接成功！");

                            }
                            else {
                                ImGui::TextColored(ImVec4(1, 0, 0, 1), "连接失败！");
                            }
                        }
                    }
                }
                else if (!g_sonos_searching) {
                    ImGui::Text("尚未搜索或未发现设备。");
                }

                // ---------- 播放控制（连接后）----------
                if (g_sonos_connect) {
                    ImGui::Separator();
                    ImGui::Text("音响已连接，回到主界面以播放音乐");
                }

                ImGui::End();
            }
        }
        if (g_netease_account_login_window_status) {
            ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("网易云登录", &g_netease_account_login_window_status, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
                // ---- 手机号行 ----
                ImGui::Text("手机号");
                ImGui::SameLine();
                ImGui::InputText("##PhoneInput", g_phoneInput, sizeof(g_phoneInput));
                ImGui::SameLine();

                // ---- 获取验证码按钮（带冷却） ----
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastCaptchaTime).count();
                bool canSend = (elapsed >= CAPTCHA_COOLDOWN) && !g_captchaSending.load();

                // 动态按钮文本
                std::string btnLabel;
                if (g_captchaSending.load()) {
                    btnLabel = "发送中...";
                }
                else if (canSend) {
                    btnLabel = "获取验证码";
                }
                else {
                    int remain = CAPTCHA_COOLDOWN - (int)elapsed;
                    btnLabel = "等待 " + std::to_string(remain) + "s";
                }

                if (ImGui::Button(btnLabel.c_str())) {
                    if (canSend) {
                        std::string phone = g_phoneInput;
                        if (phone.size() != 11) {
                            g_loginStatusMsg = "请输入有效的11位手机号";
                        }
                        else {
                            // 记录发送时刻并标记发送中
                            g_lastCaptchaTime = std::chrono::steady_clock::now();
                            g_captchaSending = true;
                            g_loginStatusMsg = "正在发送验证码...";

                            std::async(std::launch::async, [phone]() {
                                bool ok = NeteaseAPIManager::GetInstance().SendCaptcha(phone);
                                // 发送完成，解除发送中状态（冷却时间由时间控制）
                                g_captchaSending = false;
                                g_loginStatusMsg = ok ? "验证码已发送" : "发送失败，请重试";
                                });
                        }
                    }
                }

                // ---- 验证码输入行 ----
                ImGui::Text("验证码");
                ImGui::SameLine();
                ImGui::InputText("##CaptchaInput", g_captchaInput, sizeof(g_captchaInput));
                ImGui::SameLine();

                // ---- 登录按钮 ----
                if (ImGui::Button("登录")) {
                    std::string phone = g_phoneInput;
                    std::string captcha = g_captchaInput;
                    if (phone.empty() || captcha.empty()) {
                        g_loginStatusMsg = "请填写完整信息";
                    }
                    else {
                        g_loginStatusMsg = "正在登录...";
                        std::async(std::launch::async, [phone, captcha]() {
                            bool ok = NeteaseAPIManager::GetInstance().LoginByCaptcha(phone, captcha);
                            if (ok) {
                                // 保存加密 Cookie
                                std::string cookie = NeteaseAPIManager::GetInstance().GetCurrentCookie();
                                if (!cookie.empty()) {
                                    std::string encrypted;
                                    if (EncryptString(cookie, encrypted)) {
                                        cfg.SetValue("encrypted_cookie", Base64Encode(encrypted));
                                        cfg.Save();
                                    }
                                }
                                g_loginStatusMsg = "登录成功";
                                // 可关闭窗口并刷新歌单（您已有逻辑）
                                // g_netease_account_login_window_status = false;
                                // PostMessage(g_hWnd, WM_USER_RELOAD_PLAYLIST, 0, 0);
                            }
                            else {
                                g_loginStatusMsg = "登录失败，请检查验证码或网络连接";
                            }
                            });
                    }
                }

                // ---- 状态信息（自动左对齐） ----
                ImGui::Text("状态: %s", g_loginStatusMsg.c_str());
                ImGui::End();
            }
        }

        if (g_netease_account_window_status) {
            ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("网易云音乐账号详情", &g_netease_account_window_status, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
                auto netease_username = NeteaseAPIManager::GetInstance().GetNeteaseUserName();
                ImGui::Text(netease_username.c_str());
                ImGui::Separator();
                ImGui::Text("操作");
                if (ImGui::Button("退出登录")) {
                    if (ShowConfirmBox("提示", "确定要退出登录吗？")) {
                        g_netease_user_cookie.clear();
                        cfg.SetValue("encrypted_cookie", "");
                        cfg.Save();
                    }
                }
                ImGui::End();
            }
        }
        ImVec2 progressbar_size(-FLT_MIN, -FLT_MIN);
        ImGui::ProgressBar(PlayProgressPercent, ImVec2(-FLT_MIN, progress_height), "");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            float mouseX = ImGui::GetMousePos().x;
            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            float seekRatio = (mouseX - itemMin.x) / (itemMax.x - itemMin.x);
            seek(seekRatio);
        }
        // 生成右侧进度时间文本
        std::string progress_text = FormatTime(PlayCurrentMs) + " / " + FormatTime(PlayMusicLongMs);
        std::string now_playing_text = MusicMeta.title;
        if (!g_loading_music) {
            // 生成左侧歌曲信息文本
            if (!MusicMeta.artist.empty()) {
                now_playing_text += " - " + MusicMeta.artist;
            }
            if (!MusicMeta.album.empty()) {
                now_playing_text += " [ " + MusicMeta.album + " ] ";
            }
        }
        else {
            now_playing_text = GetStatusText();
        }

        // 获取进度条区域
        ImVec2 barMin = ImGui::GetItemRectMin();
        ImVec2 barMax = ImGui::GetItemRectMax();
        float barWidth = barMax.x - barMin.x;

        // 计算右侧文本的宽度（加上右侧边距6px）
        ImVec2 rightTextSize = ImGui::CalcTextSize(progress_text.c_str());
        float rightTextWidth = rightTextSize.x + 6.0f;  // 右侧留6px边距

        // 左侧文本可用宽度 = 总宽度 - 右侧文本宽度 - 边距
        float leftAvailableWidth = barWidth - rightTextWidth - 24.0f;

        std::string display_left_text = now_playing_text;

        // 如果连 "..." 都放不下，直接不显示
        float ellipsisWidth = ImGui::CalcTextSize("...").x;
        if (leftAvailableWidth <= ellipsisWidth) {
            display_left_text = "";
        }
        // 如果左侧文本超长，开始裁剪
        else if (ImGui::CalcTextSize(display_left_text.c_str()).x > leftAvailableWidth) {
            int cut_len = (int)now_playing_text.length();
            float max_text_width = leftAvailableWidth - ellipsisWidth;

            // ★ 从后往前遍历寻找安全截断点，绝对不会死循环
            while (cut_len > 0) {
                // 安全 UTF-8 边界检查：如果切到了汉字的中间字节，继续往前退
                while (cut_len > 0 && (now_playing_text[cut_len] & 0xC0) == 0x80) {
                    cut_len--;
                }

                std::string sub = now_playing_text.substr(0, cut_len);
                if (ImGui::CalcTextSize(sub.c_str()).x <= max_text_width) {
                    break; // 找到合适长度了，退出
                }
                cut_len--; // 还是太长，继续往前退
            }
            display_left_text = now_playing_text.substr(0, cut_len) + "...";
        }

        // 计算文字垂直居中位置
        float textY = barMin.y + (barMax.y - barMin.y - ImGui::CalcTextSize("A").y) * 0.5f;

        // 绘制
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // ★ 加上物理裁剪，超出进度条左侧区域的文字会被 GPU 直接切掉，绝对不越界
        drawList->PushClipRect(barMin, ImVec2(barMax.x - rightTextWidth - 6.0f, barMax.y), true);

        // 绘制左侧文本（左对齐，左边距6px）
        if (!display_left_text.empty()) {
            drawList->AddText(ImVec2(barMin.x + 6.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), display_left_text.c_str());
        }

        drawList->PopClipRect();

        // 绘制右侧文本（右对齐，右边距6px）
        float rightTextX = barMax.x - rightTextSize.x - 6.0f;
        drawList->AddText(ImVec2(rightTextX, textY), ImGui::GetColorU32(ImGuiCol_Text), progress_text.c_str());
        ImGui::EndChild();

    }
    ImGui::EndChild();
    float paneHeight = ImGui::GetWindowHeight() - (8.0f) * 2;
    // 同一行绘制分割条
    ImGui::SameLine();
    if (g_resizingSplitter) {
        ImGui::PushStyleColor(ImGuiCol_Button, g_accent);
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.05f));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_accent);
    ImGui::Button("##Splitter", ImVec2(splitterWidth, paneHeight));
    if (ImGui::IsItemActive()) {
        // 正在拖拽分割条
        g_resizingSplitter = true;
        float delta = ImGui::GetIO().MouseDelta.x;
        float newLeftWidth = leftWidth + delta;
        // 限制最小宽度（例如 300px）和最大宽度（例如 winSize.x - 200px）
        newLeftWidth = (std::max)(4.0f, (std::min)(newLeftWidth, winSize.x - (8.0f) * 2 - 4.0f));
        g_leftWidthRatio = newLeftWidth / winSize.x;
    }
    else {
        g_resizingSplitter = false;
    }
    ImGui::PopStyleColor(3);

    // 右侧区域（歌词 + 封面）
    // 右侧区域（歌词 + 封面）
    // 右侧区域（歌词 + 封面）
    ImGui::SameLine();
    ImGui::BeginChild("RightPane", ImVec2(rightWidth, 0), true);
    {
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 mousePos = ImGui::GetMousePos();

        // 手动判断鼠标是否在窗口内
        bool isHovered = (mousePos.x >= winPos.x && mousePos.x <= winPos.x + winSize.x &&
            mousePos.y >= winPos.y && mousePos.y <= winPos.y + winSize.y);

        float wheel = ImGui::GetIO().MouseWheel;
        if (isHovered && wheel != 0.0f)
        {
            g_scrollBlockCounter = 300;
        }


        // 仅大于0时递减，0永远不动
        if (g_scrollBlockCounter > 0)
        {
            g_scrollBlockCounter--;
        }
        // ================================================================================

        if (ImGui::BeginListBox("##LyricsList", ImVec2(-FLT_MIN, -FLT_MIN)))
        {
            if (g_lyrics.empty())
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "暂无歌词");
            }
            else
            {
                for (int i = 0; i < (int)g_lyrics.size(); ++i)
                {
                    bool isCurrent = (i == g_currentLyricIndex);

                    // 根据是否为当前行选择合适的字体
                    if (isCurrent && g_FontLarge)
                        ImGui::PushFont(g_FontLarge);
                    else if (g_FontNormal)
                        ImGui::PushFont(g_FontNormal);
                    else
                        ImGui::PushFont(ImGui::GetFont()); // 后备字体

                    // ★ 记录当前行的本地 Y 坐标（在画出来之前取）
                    float itemY = ImGui::GetCursorPosY();

                    std::string uniqueLabel = g_lyrics[i].second + "##" + std::to_string(i);
                    if (SelectableWithStyle(uniqueLabel.c_str(), isCurrent, 6.0f))
                    {
                        float targetPercent = (double)g_lyrics[i].first / PlayMusicLongMs;
                        seek(targetPercent);
                    }

                    // 关键：恢复字体
                    ImGui::PopFont();

                    // ★ 计算目标滚动位置
                    if (isCurrent)
                    {
                        // 只有在被允许滚动、或者刚切换歌曲/跳转时（g_needScrollToLyric），才更新目标
                        if (g_scrollBlockCounter == 0)
                        {
                            float winHeight = ImGui::GetWindowHeight();
                            // 居中公式：当前行Y - 窗口一半 + 当前行高度一半
                            g_lyricsTargetY = itemY - winHeight * 0.5f + ImGui::GetTextLineHeight() * 0.5f;

                            // 限制不能超过列表的有效滚动范围
                            g_lyricsTargetY = std::clamp(g_lyricsTargetY, 0.0f, ImGui::GetScrollMaxY());

                            g_needScrollToLyric = false;
                        }
                    }
                }
            }

            // ★ 核心：使用指数平滑算法执行缓动
            if (g_scrollBlockCounter == 0)
            {
                float currentScrollY = ImGui::GetScrollY();
                float distance = g_lyricsTargetY - currentScrollY;

                // 如果距离极微小，直接吸附，不需要渲染了
                if (std::abs(distance) > 0.05f)
                {
                    // 这里的 10.0f 是平滑速度（Smooth Time），数值越大跟随越快
                    // 1.0f - exp(-speed * dt) 是标准的帧率无关缓动公式，末期减速极其自然
                    float alpha = 1.0f - std::exp(-10.0f * ImGui::GetIO().DeltaTime);

                    float nextScrollY = currentScrollY + distance * alpha;

                    // 如果逼近程度极度接近，强制贴合（但不用再跳变了，因为距离已经小于 0.05 了）
                    if (std::abs(g_lyricsTargetY - nextScrollY) < 0.1f) {
                        nextScrollY = g_lyricsTargetY;
                    }

                    ImGui::SetScrollY(nextScrollY);
                }
            }
            else
            {
                // 用户滚轮时同步目标
                g_lyricsTargetY = ImGui::GetScrollY();
            }

            ImGui::EndListBox();
        }
    }
    ImGui::EndChild();



    ImGui::End();

    // 搜索窗口（浮动子窗口）
    if (g_showSearchWindow) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 120), ImVec2(300, 120));
        if (ImGui::Begin("搜索歌曲", &g_showSearchWindow, ImGuiWindowFlags_NoCollapse)) {
            // 输入框（实时搜索）
            bool searchChanged = ImGui::InputText("关键词", g_searchKeyword, sizeof(g_searchKeyword));
            if (searchChanged) {
                // 实时更新搜索结果
                g_searchResults.clear();
                std::string keyword = g_searchKeyword;
                if (!keyword.empty()) {
                    std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
                    for (int i = 0; i < g_source->GetCount(); ++i) {
                        const BaseTrackInfo* track = g_source->GetBaseTrack(i);
                        std::string title = track ? track->displayName : "";
                        std::transform(title.begin(), title.end(), title.begin(), ::tolower);
                        if (title.find(keyword) != std::string::npos) {
                            g_searchResults.push_back(i);
                        }
                    }
                }
                g_currentSearchIndex = g_searchResults.empty() ? -1 : 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("清除")) {
                g_searchKeyword[0] = '\0';
                g_searchResults.clear();
                g_currentSearchIndex = -1;
            }

            if (!g_searchResults.empty()) {
                ImGui::Separator();
                ImGui::Text("搜索结果 (%zu 项)", g_searchResults.size());

                // 显示搜索结果数量，并提供“上一个”和“下一个”按钮
                ImGui::SameLine();
                float availWidth = ImGui::GetContentRegionAvail().x;
                float buttonWidth = 80.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - buttonWidth * 2 - ImGui::GetStyle().ItemSpacing.x);
                if (ImGui::Button("上一个", ImVec2(buttonWidth, 0))) {
                    if (!g_searchResults.empty()) {
                        g_currentSearchIndex--;
                        if (g_currentSearchIndex < 0) g_currentSearchIndex = (int)g_searchResults.size() - 1;
                        g_needScrollToSong = true;
                        g_targetScrollIndex = g_searchResults[g_currentSearchIndex];
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("下一个", ImVec2(buttonWidth, 0))) {
                    if (!g_searchResults.empty()) {
                        g_currentSearchIndex++;
                        if (g_currentSearchIndex >= (int)g_searchResults.size()) g_currentSearchIndex = 0;
                        g_needScrollToSong = true;
                        g_targetScrollIndex = g_searchResults[g_currentSearchIndex];
                    }
                }

                // 可选：显示当前匹配项序号（不展示具体列表，避免与滚动冲突）
                ImGui::Text("当前: %d / %d", g_currentSearchIndex + 1, (int)g_searchResults.size());
            }
            else if (strlen(g_searchKeyword) > 0) {
                ImGui::Text("未找到匹配的歌曲");
            }
        }
        ImGui::End();
    }
    else {
        // 窗口关闭时，清除所有搜索状态（关键词、结果、滚动标志）
        if (g_searchKeyword[0] != '\0') {
            g_searchKeyword[0] = '\0';
            g_searchResults.clear();
            g_currentSearchIndex = -1;
            g_needScrollToSong = false;
            g_targetScrollIndex = -1;
        }
    }

    // 渲染 D3D11
    ImGui::Render();
}
void StartSearchSonos() {
    g_searchFuture = std::async(std::launch::async, []() {
        SonosController ctrl;   // 每个线程独立控制器，避免共享状态问题
        ctrl.Initialize();
        return ctrl.SearchDevices();
        });
    g_sonos_searching = true;
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
    ReadConfig();

    RegisterMusicSources();
    if (!g_apiAddr.empty() && g_online_player) {
        NeteaseAPIManager::GetInstance().SetBaseURL(g_apiAddr);
    }
    else if(g_online_player) {
        ShowMessageBox("退出", "未配置网易云歌单ID或API地址配置错误（有可能已经失效），程序将退出");
        PostMessage(g_hWnd, WM_CLOSE, 0, 0);
    }
    if (!g_sonos) {
        g_sonos = std::make_unique<SonosController>();
        g_sonos->Initialize();
    }
    srand(time(NULL));
    if (FAILED(hr)) {
        // 如果设置失败，可以输出调试信息，但不影响程序继续运行
        OutputDebugString(L"Failed to set AppUserModelID\n");
    }
    initMusicList();//音乐源在这里初始化
    if (!InitPlayerEngine())return 1;
    try { winrt::init_apartment(); }//winrt初始化
    catch (...) { OutputDebugStringA("winrt没有初始化\n"); }
    InitUserCookie();
    static SettingWindow settings;
    if (!InitWindow(CLASS_NAME)) return 1;
    InitHotkey(g_hWnd);
    InitSMTC(g_hWnd);
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
            /*else if (g_sonos_connect && IsPlaying == true && PlayCurrentMs >= PlayMusicLongMs - 100) {
                AutoPlayNextSong();
            }*/
            //else if (g_sonos_connect && IsPlaying) {
            //    if (counter == 30) {
            //        static int range_time = 0;
            //        range_time++;
            //        if (range_time >= 10) {
            //            range_time = 0;
            //        }
            //        auto player = g_sonos->GetPlayer();
            //        if (player) {
            //            SONOS::ElementList vars;
            //            if (player->GetTransportInfo(vars)) {
            //                std::string state = vars.GetValue("CurrentTransportState");
            //                if (state == "STOPPED" || state == "PAUSED_PLAYBACK") {
            //                    AutoPlayNextSong();
            //                }
            //            }
            //        }
            //    }
            //}
        }
        if ((g_IsSoundLoaded && !g_lyrics.empty()) || (g_sonos_connect == true && g_loading_music == false && !g_lyrics.empty())) {
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
        // 大约每 1 秒更新一次即可，太频繁会卡
        if (counter % 60 == 0 && g_IsSoundLoaded) {
            UpdateSMTCProgress(PlayCurrentMs);
        }
        if (g_scrollBlockCounter > 0) {
            g_scrollBlockCounter--;
        }
        if (amllplayer && counter == 0) {
            amllplayer->sendProgress(PlayCurrentMs);
            if (IsPlaying == false) {
                amllplayer->sendPaused();
            }
            else {
                amllplayer->sendResumed();
            }
        }
        if (amllplayer && counter == 0) {
            AMLLCommand cmd = amllplayer->getNextCommand();
            switch (cmd) {
            case AMLLCommand::Pause:    ChangePlayMode(); break;
            case AMLLCommand::Resume:   ChangePlayMode(); break;
            case AMLLCommand::Forward:
                CurrentIndex++;
                UpdateCurrentPlaying(CurrentIndex);
                break;   // 你定义的函数
            case AMLLCommand::Backward:
                CurrentIndex--;
                UpdateCurrentPlaying(CurrentIndex);
                break;
            case AMLLCommand::Seek: {
                uint64_t seekPos = amllplayer->getPendingSeek();
                if (seekPos == UINT64_MAX) break;   // 没有待处理的 seek
                double ratio = static_cast<double>(seekPos) / PlayMusicLongMs;
                if (ratio < 0.0) ratio = 0.0;
                if (ratio > 1.0) ratio = 1.0;
                OutputDebugStringA(("AMLL-seek-ratio: " + std::to_string(ratio) + "\n").c_str());
                seek(ratio);
                break;
            }
            }
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
        RenderFrame(counter, settings);
        
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
        g_userWheel = true;
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