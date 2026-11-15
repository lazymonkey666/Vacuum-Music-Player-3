#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <imgui/imgui.h>

#include "StateVar.h"       // 包含我们之前写的 StateVar / StateObject
#include "MusicSource.h"
#include "PlaybackBackend.h"
#include "platform/platform.h"
#include "ManageConfig.h"

// ── 1. 全局结构体与枚举 ──
enum StatusCode {
    STATUS_FETCHING_URL = 1,
    STATUS_DOWNLOADING = 2,
    STATUS_WRITING_TAGS = 3,
    STATUS_FETCHING_META = 4,
    STATUS_ERROR = -1,
    STATUS_FETCHING_INFORMATION = 10
};

struct MusicMetadata {
    std::string title = "Unknown Title";
    std::string artist = "Unknown Artist";
    std::string album = "Unknown Album";
    TextureHandle albumArtSRV = nullptr;
};

struct AlbumArtAsyncResult {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgbaData;
    std::vector<unsigned char> rawData;
    std::string title;
    std::string artist;
    std::string album;
    int targetIndex = -1;
};

struct AsyncPlayResult {
    int taskId = 0;
    bool success = false;
    std::string localPath;
    std::string errorMsg;
    int index = -1;
};

// ── 2. 播放源与配置 ──
inline ManageConfig& cfg = ManageConfig::GetInstance();
inline std::shared_ptr<IMusicSource> g_source = nullptr; // 用 shared_ptr 替代 unique_ptr 以支持跨线程安全拷贝

// ── 3. 核心播放状态（原子保护） ──
inline StateVar<int>      CurrentIndex{ -1 };
inline StateVar<bool>     IsPlaying{ false };
inline StateVar<uint64_t> PlayCurrentMs{ 0 };
inline StateVar<float>    PlayMusicLong{ 0.0f };
inline StateVar<uint64_t> PlayMusicLongMs{ 1 };
inline StateVar<float>    PlayProgressPercent{ 0.0f };
inline StateVar<bool>     g_loading_music{ false };
inline StateVar<int>      g_PlayOrder{ 0 };
inline StateVar<int>      g_statusId{ 1 };

// ── 4. 异步任务 ID ──
inline StateVar<int>      g_AsyncTaskId_search{ 0 };
inline StateVar<int>      g_AsyncTaskId_download{ 0 };
inline StateVar<uint64_t> g_latestLoadTask{ 0 };
inline StateVar<uint64_t> g_latestPlayTask{ 0 };
inline StateVar<uint64_t> g_latestPreLoadId{ 0 };

// ── 5. 跨线程共享数据（读写锁保护） ──
inline MusicMetadata MusicMeta;
inline std::vector<std::pair<int, std::string>> g_lyrics;
inline StateVar<int>      g_currentLyricIndex{ -1 };
inline StateVar<bool>     g_needScrollToLyric{ false };

// 播放缓存（用于 PostPlay 回填）
inline StateObject<std::string> g_curTitle;
inline StateObject<std::string> g_curArtist;
inline StateObject<std::string> g_curAlbum;
inline StateObject<std::string> g_curLyrics;
inline StateObject<std::vector<unsigned char>> g_curAlbumArt;
inline StateObject<std::string> g_currentPlayingTrackId;

// ── 6. 配置、主题与常量（大部分初始化后只读，原子保护） ──
inline StateVar<int>      g_opacity{ 128 };
inline StateVar<bool>     g_enableSnapToEdge{ true };
inline StateVar<bool>     g_online_player{ false };
inline StateVar<bool>     g_skip_vip{ false };
inline StateVar<bool>     g_cache_clean{ true };
inline StateVar<int>      g_cache_limit_mb{ 2048 };
inline StateVar<bool>     g_enable_lyric_translate_split{ true };
inline StateVar<bool>     g_needUpdateStyle{ false };
inline StateVar<DWORD>    g_iconColor{ 0x222222 };
inline StateObject<std::string> g_dark_mode{ "System" };
inline StateObject<std::string> g_apiAddr;
inline StateObject<std::string> g_activeThemeZip;
inline StateVar<bool>     g_bRenderSuspended{ false };

// ── 7. 全局资源字典（主线程专属） ──
inline std::unordered_map<std::string, TextureHandle> g_iconCache;
inline ImVec4 g_accent;
inline ImFont* g_FontNormal = nullptr;
inline ImFont* g_FontLarge = nullptr;
inline UINT g_uTaskbarBtnMsg = 0;
inline std::vector<std::string> g_RemainPlaylist;

// ── 8. 窗口动画、拖拽与吸附（主线程专属，不需要多线程保护） ──
inline bool     g_isAnimating = false;
inline bool     g_targetHidden = false;
inline int      g_animStep = 0;
inline int      g_animTotalSteps = 15;
inline bool     g_userWheel = false;

// ★ 直接用普通变量，彻底避开模板/原子类的约束
inline RECT     g_animStartRect = { 0, 0, 0, 0 };
inline RECT     g_animTargetRect = { 0, 0, 0, 0 };
inline BYTE     g_animStartAlpha = 255;
inline BYTE     g_animTargetAlpha = 255;
inline UINT_PTR g_animTimer = 0;
inline bool     g_isWindowHidden = false;
inline int      g_displayX = 0;
inline int      g_displayY = 0;
inline int      g_windowSide = 0;
inline int      g_currentAlpha = 255;

inline int      g_animStartWidth = 0, g_animTargetWidth = 0;
inline int      g_animStartHeight = 0, g_animTargetHeight = 0;
inline int      g_normalWidth = 700;
inline int      g_normalHeight = 230;
inline int      g_snapThreshold = 10;
inline int      g_lastSnapSide = -1;

inline bool     g_isDragging = false;
inline POINT    g_dragStartPos = { 0, 0 };
inline POINT    g_windowStartPos = { 0, 0 };


inline std::vector<PlaybackDeviceInfo> s_cachedSonosDevices;
inline StateVar<int> s_selectedSonosDeviceIdx{ -1 };

// 延迟释放的旧封面句柄
inline StateVar<TextureHandle> g_pendingReleaseTexture{ nullptr };

// ── 9. 辅助函数与工具 ──
inline IPlaybackBackend* ActiveBackend() {
    return PlaybackBackendRegistry::GetInstance().Active();
}

inline bool IsSonosActive() {
    return PlaybackBackendRegistry::GetInstance().ActiveId() == "sonos";
}

inline void SetStatus(int id) { g_statusId.Set(id); }

inline const char* GetStatusText() {
    switch (g_statusId.Get()) {
    case STATUS_FETCHING_URL:   return "正在获取播放链接";
    case STATUS_DOWNLOADING:    return "正在下载歌曲";
    case STATUS_WRITING_TAGS:   return "正在写入标签";
    case STATUS_FETCHING_META:  return "正在获取元数据";
    case STATUS_FETCHING_INFORMATION: return "正在获取歌曲信息";
    default:                    return "未知";
    }
}