#include "pch.h"
#include "Initialize.h"
#include "AppState.h"
#include <PlaybackStateMachine.h>
#include "utils.h"
#include "MainUI.h"
#include <TagReader.h>

// 内部插件
#include "MusicSource.h"
#include "PlaybackBackend.h"
#include "PlaybackService.h"
#include <SonosService.h> 
#include "AsyncEventManager.h"
#include "SettingItem.h"
#include "ManageConfig.h"
#include "IconManager.h"
#include "ThemePack.h"
#include "NeteaseAPIManager.h"
#include <WindowAnimator.h>

// 外部业务函数声明（后续抽离到 PlaybackController 中）
extern void AutoPlayNextSong();
extern void ApplyTheme(const std::string& mode, int opacity);


namespace AppInit {

    // ═══════════════════════════════════════════════════════════
    // 内部辅助函数
    // ═══════════════════════════════════════════════════════════
    static void RegisterMusicSources() {
        auto& reg = MusicSourceRegistry::GetInstance();
        reg.Register({ "local",  "本地播放",       CreateLocalSource });
        reg.Register({ "online", "在线[网易云]", CreateNeteaseSource });
    }

    static void RegisterPlaybackSystems() {
        PlaybackBackendRegistry::GetInstance().Register(
            { "miniaudio", "本机扬声器", CreateMiniaudioBackend });
        PlaybackBackendRegistry::GetInstance().Register(
            { "sonos",     "Sonos 音响", CreateSonosBackend });
    }

    static void RegisterServices() {
        auto& reg = PlaybackServiceRegistry::GetInstance();
        reg.Register({ "smtc", "系统媒体控制", "", true, CreateSmtcService, false });
        reg.Register({ "amll", "AMLL 歌词同步", "service_amll_enabled", false, CreateAMLLService, true });
        reg.Register({ "sonos", "Sonos 音响", "service_sonos_enabled", false, CreateSonosService, true });
    }

    static bool InitPlaybackBackend() {
        std::string id = cfg.GetValue<std::string>("playback_backend", "miniaudio");
        if (!PlaybackBackendRegistry::GetInstance().Activate(id)) {
            if (!PlaybackBackendRegistry::GetInstance().Activate("miniaudio")) {
                ShowMessageBox("错误", "播放后端初始化失败");
                return false;
            }
        }
        return true;
    }
    void ConfigureStateMachine() {

        g_sm.SetBridges({
            // ── 资源准备：缓存检查 → 下载 → 读标签 → 封面 ──
            .prepare = [](int index, std::function<void(PrepareResult)> onDone) {
    std::thread([index, onDone = std::move(onDone)]() {
        PrepareResult r;
        try {
            if (!g_source) { r.error = "no source"; }
            else if (!g_source->GetBaseTrack(index)) { r.error = "index out of range"; }
            else {
                const BaseTrackInfo* track = g_source->GetBaseTrack(index);

                // ① 命中缓存？
                PathType cacheW = GetCacheDirectory();
                PathType pathMp3 = cacheW + Utf8ToPath(track->id + ".mp3");
                PathType pathFlac = cacheW + Utf8ToPath(track->id + ".flac");
                std::error_code ec;
                std::string local;
                if (std::filesystem::exists(pathFlac, ec) && !ec)
                    local = PathToUtf8(pathFlac);
                else if (std::filesystem::exists(pathMp3, ec) && !ec)
                    local = PathToUtf8(pathMp3);

                // ② 未命中：下载
                if (local.empty()) {
                    SetStatus(STATUS_FETCHING_INFORMATION);
                    PlaybackInfo info = g_source->PreparePlayback(index);
                    if (!info.isValid) r.error = "invalid playback info";
                    else {
                        local = DownloadAndProcessTrack(info);
                        if (local.empty()) r.error = "download failed";
                    }
                }

                // ③ 读元数据 / 歌词 / 封面
                if (local.empty() && r.error.empty()) r.error = "unknown";
                if (r.error.empty()) {
                    PathType w = Utf8ToPath(local);
                    auto meta = GetMusicMetadata(local);
                    std::string lrc = GetLyricsFromFile(w);
                    if (lrc.empty()) {
                        PathType lp = FindLrcFile(w);
                        if (!lp.empty()) lrc = ReadTextFile(lp);
                    }

                    r.ok = true;
                    r.localPath = local;
                    r.title = meta.title;
                    r.artist = meta.artist;
                    r.album = meta.album;
                    r.lyrics = lrc;
                    r.albumArt = ExtractAlbumArt(w);
                    r.durationMs = GetAudioFileDurationMs(local);
                }
            }
        }
 catch (const std::exception& e) {
  r.ok = false; r.error = e.what();
}
catch (...) {
 r.ok = false; r.error = "unknown exception";
}

// ★ 关键：回到主线程再调 onDone
AsyncEventManager::GetInstance().PostToMainThread(
    [onDone = std::move(onDone), r = std::move(r)]() mutable {
        onDone(std::move(r));
    });
}).detach();
},

// ── 后端：直接转发给 ActiveBackend() ──
.requestLoad = [](const std::string& localPath, int index) -> uint64_t {
    auto* be = ActiveBackend();
    if (!be) return 0;
    return be->RequestLoad(Utf8ToPath(localPath), (uint64_t)index);
},
.requestPlay = [](int index) -> uint64_t {
    auto* be = ActiveBackend();
    return be ? be->RequestPlay((uint64_t)index) : 0;
},
.requestPause = [](int index) -> uint64_t {
    auto* be = ActiveBackend();
    return be ? be->RequestPause((uint64_t)index) : 0;
},
.requestSeek = [](uint64_t posMs) -> uint64_t {
    auto* be = ActiveBackend();
    return be ? be->RequestSeek(posMs) : 0;
},
.requestUnload = []() {
    auto* be = ActiveBackend();
    if (be && be->IsLoaded()) be->RequestUnload();
},
            });
    }

    static void initMusicList() {
        std::string playmode = cfg.GetValue<std::string>("play_mode", "local");
        g_source = MusicSourceRegistry::GetInstance().Create(playmode);
        if (!g_source) {
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

    static void ReadConfig() {
        cfg.Load();
        auto playmode = cfg.GetValue<std::string>("play_mode", "local");
        g_online_player = (playmode == "online");
        g_apiAddr = cfg.GetValue<std::string>("netease_api_address", "");
        g_skip_vip = cfg.GetValue<bool>("skip_vip", false);
        g_cache_clean = cfg.GetValue<bool>("enable_cache_limit", true);
        g_cache_limit_mb = cfg.GetValue<int>("cache_limit_mb", 2048);
        g_enable_lyric_translate_split = cfg.GetValue<bool>("enable_lyric_translate_split", true);
        g_opacity = cfg.GetValue("acrylic_opacity", 128);
        if (g_opacity < 0 || g_opacity > 255) g_opacity = 128;
        g_dark_mode = cfg.GetValue<std::string>("theme_style", "System");
        g_enableSnapToEdge = cfg.GetValue<bool>("enable_snap_to_edge", true);
        g_activeThemeZip = cfg.GetValue<std::string>("active_theme_zip", "");
    }

    static void LoadTheme() {
        if (!g_activeThemeZip.Get().empty()) {
            std::string id = ThemePackManager::GetInstance().LoadFromZipAbsolute(g_activeThemeZip.Get());
            if (id.empty()) {
                OutputDebugStringA(("加载主题包失败: " + g_activeThemeZip.Get() + "\n").c_str());
            }
        }
        ThemePackManager::GetInstance().LoadActiveBackgroundTexture();
    }

    static bool InitWindowPlatform(const std::string& className) {
        if (!InitWindowPlat(className)) return false;

        WindowAnimator::Get().Init(g_hWnd);
        WindowAnimator::Get().Reset();
        MainUI::g_NeedScrollToCurrent = true;

        int lastX = cfg.GetValue<int>("window_x", -1);
        int lastY = cfg.GetValue<int>("window_y", -1);
        if (lastX != -1 && lastY != -1) {
            RECT workArea = GetWorkAreaPlat();
            RECT winRect = GetWindowRectPlat(g_hWnd);
            int winWidth = winRect.right - winRect.left;
            int winHeight = winRect.bottom - winRect.top;
            lastX = (std::max)((int)workArea.left, (std::min)(lastX, (int)(workArea.right - winWidth)));
            lastY = (std::max)((int)workArea.top, (std::min)(lastY, (int)(workArea.bottom - winHeight)));
            MoveWindowPlat(g_hWnd, lastX, lastY);
        }

        WindowAnimator::Get().UpdateSide();
        ApplyTheme(g_dark_mode.Get(), g_opacity);
        SetWindowAlpha(g_hWnd, 255);

        if (!CreateDeviceD3D(g_hWnd)) {
            ShowMessageBox("错误", "DX11 初始化失败");
            return false;
        }
        return true;
    }

    // ═══════════════════════════════════════════════════════════
    // 对外暴露的初始化入口
    // ═══════════════════════════════════════════════════════════

    bool InitCore() {
        ReadConfig();

        RegisterMusicSources();
        RegisterPlaybackSystems();
        RegisterServices();

        // 如果 AsyncHooks 和外部业务耦合太深，可暂时留在 main.cpp，后续再抽到 PlaybackController
        // RegisterAsyncHooks();

        AsyncEventManager::GetInstance().Start();
        PlaybackServiceRegistry::GetInstance().EnableDefaults();

        if (!g_apiAddr.Get().empty() && g_online_player) {
            NeteaseAPIManager::GetInstance().SetBaseURL(g_apiAddr.Get());
        }
        else if (g_online_player) {
            ShowMessageBox("退出", "未配置网易云歌单ID或API地址配置错误，程序将退出");
            PostMessage(g_hWnd, WM_CLOSE, 0, 0);
        }

        initMusicList();
        if (g_source) g_source->SyncPlaylistAsync();

        if (!InitPlaybackBackend()) return false;
        ConfigureStateMachine();

        return true;
    }

    bool InitUI(const std::string& className) {
        if (!InitWindowPlatform(className)) return false;

        InitHotkey(g_hWnd);
        InitSMTC(g_hWnd);
        PrepareImGui();
        LoadTheme();

        if (ThemePackManager::GetInstance().GetActive()) {
            g_iconCache.clear();
            MainUI::UpdateImGuiStyle();
        }

        IconManager::GetInstance().Initialize();
        IconManager::GetInstance().SetThemeColor(g_iconColor);

        RECT rc;
        GetWindowRect(g_hWnd, &rc);
        g_normalWidth = rc.right - rc.left;
        g_normalHeight = rc.bottom - rc.top;

        return true;
    }

    bool SwitchPlaybackBackend(const std::string& id) {
        auto& reg = PlaybackBackendRegistry::GetInstance();
        if (reg.ActiveId() == id) return true;
        if (!reg.Activate(id)) return false;
        cfg.SetValue("playback_backend", id);
        cfg.Save();
        return true;
    }

}

