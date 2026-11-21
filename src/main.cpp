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

#include <PlaybackStateMachine.h>

#include <PlaybackView.h>

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


#define WM_FETCH_READY (WM_APP + 10)

#define WM_USER_SOFTWARE_RESTART (WM_APP+100)

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





//std::wstring s2ws(const std::string& str) {//万年老函数，老资历 ！
//    return Utf8ToPath(str);
//}


void PlayMusicFile(int index) {
    if (!g_source || index < 0 || index >= (int)g_source->GetCount()) return;

    // ① UI 副作用（状态机不管）
    SetStatus(STATUS_FETCHING_URL);
    MainUI::g_scrollBlockCounter = 0;

    // ② 预读标签（滚动条附近的 50 首）
    int totalCount = (int)g_source->GetCount();
    int startIdx = (index - 19 < 0) ? 0 : index - 19;
    int endIdx = (index + 30 > totalCount) ? totalCount : index + 30;
    g_source->RequestTrackDetails(startIdx, endIdx);

    // ③ 保存索引
    g_source->SaveIndex(index);

    // ④ 交给状态机（后面全自动）
    const BaseTrackInfo* track = g_source->GetBaseTrack(index);
    g_sm.Play(index, track ? track->id : "");
}

void UpdateCurrentPlaying(int new_index) {
    if (!g_source || g_source->GetCount() == 0) return;

    if (new_index < 0 || new_index >= (int)g_source->GetCount())
        CurrentIndex = 0;
    else
        CurrentIndex = new_index;

    PlayMusicFile(CurrentIndex);
    // ❌ 删掉：IsPlaying = true;   // 现在由状态机管
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
    // ❌ 原来那一大坨 RequestPlay/RequestPause + 乐观 IsPlaying 全删
    g_sm.TogglePlay();
    return g_sm.IsPlaying();
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

static void ScheduleAlbumArtUpdate() {
    const auto& s = g_sm.Snap();
    int taskId = ++g_AsyncTaskId_search;

    // ① 空封面：直接发空结果，让 UI 释放旧纹理
    if (s.albumArt.empty()) {
        auto* r = new AlbumArtAsyncResult();
        r->targetIndex = taskId;
        if (g_hWnd)
            PostMessage(g_hWnd, WM_UPDATE_ALBUM_ART, 0, reinterpret_cast<LPARAM>(r));
        else
            delete r;   // 窗口已关，别泄漏
        return;
    }

    // ② 拷贝一份给后台线程（s 是引用，不能跨线程持有）
    auto        raw = s.albumArt;   // vector<unsigned char>
    std::string t = s.title;
    std::string a = s.artist;
    std::string al = s.album;

    std::thread([raw = std::move(raw), t, a, al, taskId]() {
        // 过期丢弃（快速切歌时，只有最新 taskId 生效）
        if (taskId != g_AsyncTaskId_search) return;

        auto proc = ProcessAlbumArtWithGradient(raw, 230, 230);
        if (proc.empty() || taskId != g_AsyncTaskId_search) return;

        auto* r = new AlbumArtAsyncResult();
        r->width = 230;
        r->height = 230;
        r->rgbaData = std::move(proc);
        r->rawData = raw;
        r->title = t;
        r->artist = a;
        r->album = al;
        r->targetIndex = taskId;

        if (g_hWnd)
            PostMessage(g_hWnd, WM_UPDATE_ALBUM_ART, 0, reinterpret_cast<LPARAM>(r));
        else
            delete r;
        }).detach();
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
        if (g_source) {
            int targetIdx = -1;
            const std::string& curId = g_sm.Snap().trackId;
            if (!curId.empty()) {
                targetIdx = g_source->FindTrackIndex(curId);
            }
            else {
                // ★ 初始化时 g_currentPlayingTrackId 还是空：用 cfg 里的 last 兜底
                targetIdx = g_source->GetLastIndex();
            }
            if (targetIdx >= 0 && targetIdx != CurrentIndex) {
                CurrentIndex = targetIdx;
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
    auto st = g_sm.Snap().state;
    if (st != PlayState::Playing && st != PlayState::Paused) return false;
    g_sm.Seek(seekRatio);
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
    // PreLoad：读元数据 / 歌词 / 封面（worker 线程执行）-> 已经在initialize.cpp中注册 此处无代码
    // ═══════════════════════════════════════════════════════


    // ═══════════════════════════════════════════════════════
    // BackendLoaded：后端加载成功 → 发起播放
    em.Subscribe(AsyncEventType::BackendLoaded,
        [](AsyncEventPayload& p) -> EventOutcome {
            // ❌ 删掉：if (p.extraInt != g_latestLoadTask) return Skipped;
            g_sm.OnBackendLoaded(p.extraInt, true, p.durationMs, "");
            return EventOutcome::Handled;
        });

    // ═══════════════════════════════════════════════════════
    // BackendPlayed：后端启动播放
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendPlayed, [](AsyncEventPayload& p) -> EventOutcome {
        // ❌ 删掉：if (p.extraInt != g_latestPlayTask) return Skipped;
        g_sm.OnBackendPlayed(p.extraInt, p.durationMs, p.trackIndex);

        // ★ 状态机确认进入 Playing 后，再发 PostPlay
        if (g_sm.Snap().state == PlayState::Playing) {
            const auto& s = g_sm.Snap();
            p.title = s.title;
            p.artist = s.artist;
            p.album = s.album;
            p.lyrics = s.lyrics;
            p.albumArt = s.albumArt;
            if (p.durationMs == 0) p.durationMs = g_sm.Snap().durationMs;
            AsyncEventManager::GetInstance().Post(AsyncEventType::PostPlay, p);
        }
        return EventOutcome::Handled;
        });

    // ═══════════════════════════════════════════════════════
    // BackendLoadFailed：加载失败 → UI 提示 + 状态复位
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendLoadFailed,
        [](AsyncEventPayload& p) -> EventOutcome {
            // ❌ 删掉：if (p.extraInt != g_latestLoadTask) return Skipped;
            // ❌ 删掉：g_loading_music = false;
            g_sm.OnBackendLoaded(p.extraInt, false, 0, p.errorMsg);
            return EventOutcome::Handled;
        });
    // ═══════════════════════════════════════════════════════
    // BackendEnded：自然结束 → 自动下一首
    // ═══════════════════════════════════════════════════════
    em.Subscribe(AsyncEventType::BackendEnded,
        [](AsyncEventPayload& p) -> EventOutcome {
            g_sm.OnBackendEnded(p.extraInt);
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
            uint64_t dur = g_sm.Snap().durationMs;
            if (dur == 0) return EventOutcome::Skipped;
            double ratio = (double)p.positionMs / (double)dur;
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
    // ★ 新增：绑定"歌播完了 → 算下一首"
    g_sm.onTrackEnded = []() { AutoPlayNextSong(); };

    // ★ 新增：绑定"播放失败 → 计数 + 跳过"（见步骤 ⑥ 的 lambda，从这调用）
    g_sm.onPlayFailed = []() {
        static int      consecutiveFails = 0;
        static uint64_t lastFailTime = 0;

        uint64_t now = GetTickCount64();
        // 距离上次失败超过 5 秒，认为用户重新操作了，重置计数
        if (now - lastFailTime > 5000) consecutiveFails = 0;
        lastFailTime = now;

        if (consecutiveFails < 10) {
            consecutiveFails++;
            OutputDebugStringA(("[onPlayFailed] 连续失败 " +
                std::to_string(consecutiveFails) + " 次，尝试自动切换下一首\n").c_str());

            // 延时切歌：避免请求太快被服务器风控，也避免 UI 瞬间乱跳
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (g_hWnd) PostMessage(g_hWnd, WM_APP_SMTC_NEXT, 0, 0);
                }).detach();
        }
        else {
            consecutiveFails = 0;
            ShowMessageBox("播放停止", "连续多首歌曲无法播放已停止自动跳过。");
        }
        };

    // ★ 状态机状态变化 → 只触发真正的副作用（不再镜像任何变量）
    g_sm.onStateChanged = []() {
        const auto& s = g_sm.Snap();

        // 唯一还需要侦听的：封面变了 → 触发渐变
        static std::vector<unsigned char> lastArt;
        if (s.albumArt != lastArt) {
            lastArt = s.albumArt;
            ScheduleAlbumArtUpdate();
        }
        };


    // ★ 恢复之前的修复：重排后重新定位
    if (g_source) CurrentIndex = g_source->GetLastIndex();
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

        if (g_sm.IsPlaying() && ActiveBackend() && ActiveBackend()->IsLoaded()) {
            g_sm.UpdatePosition(ActiveBackend()->GetPositionMs());
        }

        if (g_needUpdateStyle) {
            g_iconCache.clear();
            ApplyTheme(g_dark_mode.Get(), g_opacity);
            IconManager::GetInstance().SetThemeColor(g_iconColor);
            MainUI::UpdateImGuiStyle();
            g_needUpdateStyle = false;


            // 如果主题里涉及背景图，也可以在这里重新加载
            // ThemePackManager::GetInstance().LoadActiveBackgroundTexture();
        }
        if (ActiveBackend() && ActiveBackend()->IsLoaded()) {
            int newIndex = PsCurrentLyricIndex();
            // 找到最后一个时间 <= currentMs 的行
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
            PlaybackServiceRegistry::GetInstance().Tick(PsPositionMs(), PsIsPlaying());
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