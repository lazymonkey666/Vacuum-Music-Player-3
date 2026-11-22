#include <pch.h>
#include "application.h"
#include <AppState.h>
#include <initialize.h>
#include <MainUI.h>
#include <PlaybackStateMachine.h>
#include <PlaybackView.h>
#include <AsyncEventManager.h>
#include <PlaybackService.h>
#include <NeteaseAPIManager.h>
#include <ThemePack.h>
#include <IconManager.h>
#include <SonosService.h>
#include <WindowAnimator.h>
#include <utils.h>
#include <imgui/imgui.h>

#include <thread>
#include <chrono>

// ── main.cpp 提供的业务函数 ──
extern void PlayMusicFile(int index);
extern void UpdateCurrentPlaying(int new_index);
extern void AutoPlayNextSong();
extern void RegisterAsyncHooks();

Application& Application::Get() {
    static Application inst;
    return inst;
}

// ═══════════════════════════════════════════════════════════
// 平台差异（仅 3 处）
// ═══════════════════════════════════════════════════════════

bool Application::PlatformInitWindow(const std::string& className) {
#if PLATFORM_WINDOWS
    if (!AppInit::InitWindowPlatform(className)) return false;
    try { winrt::init_apartment(); }
    catch (...) { OutputDebugStringA("winrt init failed\n"); }
    return true;
#else
    if (!InitWindowPlat(className)) return false;

    // ★ Linux 需要手动做 Windows 版 AppInit::InitWindowPlatform 里的事
    WindowAnimator::Get().Init(g_hWnd);
    WindowAnimator::Get().Reset();
    MainUI::g_NeedScrollToCurrent = true;

    // 恢复上次窗口位置
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
    MainUI::ApplyTheme(g_dark_mode.Get(), g_opacity);
    SetWindowAlpha(g_hWnd, 255);

    return true;
#endif
}

bool Application::PlatformReadyToRender() {
#if PLATFORM_WINDOWS
    if (!g_pd3dDevice || !g_pSwapChain || !g_mainRenderTargetView || !g_pd3dDeviceContext)
        return false;
    if (g_pd3dDevice->GetDeviceRemovedReason() != S_OK) {
        RecreateD3DDevice(g_hWnd);   // ★ 立刻恢复
        return false;
    }
    return true;
#else
    return true;
#endif
}


// ═══════════════════════════════════════════════════════════
// 入口
// ═══════════════════════════════════════════════════════════

int Application::Run(int argc, char** argv) {
    (void)argc; (void)argv;

#if PLATFORM_WINDOWS
    g_hMutex = CreateMutexA(NULL, FALSE, "LazyMonkey666_VacuumMusicPlayer_Mutex");
    if (g_hMutex == NULL) { ShowMessageBox("错误", "程序创建失败"); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ShowMessageBox("提示", "程序已经在运行中");
        CloseHandle(g_hMutex);
        return 0;
    }
    SetCurrentProcessExplicitAppUserModelID(L"LazyMonkey666.VacuumMusicPlayer");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        ShowMessageBox("错误", "WSAStartup 失败");
        return 1;
    }
#else
    if (!AcquireSingleInstanceLock()) { ShowMessageBox("提示", "程序已经在运行中"); return 0; }
    if (!InitNetwork()) { ShowMessageBox("错误", "网络初始化失败"); return 1; }
#endif

    RegisterAsyncHooks();

    if (!AppInit::InitCore()) return 1;
    if (!InitUI()) return 1;

    // 状态机三段回调
    g_sm.onTrackEnded = []() { AutoPlayNextSong(); };

    g_sm.onPlayFailed = []() {
        static int      consecutiveFails = 0;
        static uint64_t lastFailTime = 0;
        uint64_t now = GetTickCount64();
        if (now - lastFailTime > 5000) consecutiveFails = 0;
        lastFailTime = now;

        if (consecutiveFails < 10) {
            consecutiveFails++;
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (g_hWnd) PostMessage(g_hWnd, WM_APP_SMTC_NEXT, 0, 0);
                }).detach();
        }
        else {
            consecutiveFails = 0;
            OutputDebugStringA("[onPlayFailed] 连续 10 首失败，停止\n");
        }
        };

    g_sm.onStateChanged = []() {
        const auto& s = g_sm.Snap();
        static std::vector<unsigned char> lastArt;
        if (s.albumArt != lastArt) {
            lastArt = s.albumArt;
            extern void ScheduleAlbumArtUpdate();
            ScheduleAlbumArtUpdate();
        }
        };

    if (g_source) CurrentIndex = g_source->GetLastIndex();
    UpdateCurrentPlaying(CurrentIndex);

    int r = MainLoop();

    CleanupNetwork();
#if PLATFORM_LINUX
    ReleaseSingleInstanceLock();
#endif
    return r;
}

// ═══════════════════════════════════════════════════════════
// 分步初始化
// ═══════════════════════════════════════════════════════════

bool Application::InitUI() {
    if (!PlatformInitWindow("VacuumMusicPlayer")) return false;
    m_hwnd = g_hWnd;

    InitHotkey(m_hwnd);
    InitSMTC(m_hwnd);
    PrepareImGui();
    AppInit::LoadTheme();

    if (ThemePackManager::GetInstance().GetActive()) {
        g_iconCache.clear();
        MainUI::UpdateImGuiStyle();
    }
    IconManager::GetInstance().Initialize();
    IconManager::GetInstance().SetThemeColor(g_iconColor);
    InitTaskbarProgress(m_hwnd);

    // 窗口尺寸记录（水平/垂直贴边动画要用）
    RECT rc = GetWindowRectPlat(m_hwnd);
    g_normalWidth = rc.right - rc.left;
    g_normalHeight = rc.bottom - rc.top;
    return true;
}

// ═══════════════════════════════════════════════════════════
// 主循环
// ═══════════════════════════════════════════════════════════

int Application::MainLoop() {
    MSG msg{};
    while (true) {

#if PLATFORM_LINUX
        LnxProcessTimers();
#endif

        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
#if PLATFORM_LINUX
            extern bool LnxProc(HWND, UINT, WPARAM, LPARAM);
            LnxProc(g_hWnd, msg.message, msg.wParam, msg.lParam);
#else
            TranslateMessage(&msg);
            DispatchMessage(&msg);
#endif
            continue;
        }

        if (g_needUpdateStyle) {
            g_needUpdateStyle = false;

            // ① 先清图标缓存（这里只丢指针，IconManager 负责 Release）
            g_iconCache.clear();
            IconManager::GetInstance().SetThemeColor(g_iconColor);

            // ② 让 DWM 先稳一下，再改 acrylic
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            MainUI::ApplyTheme(g_dark_mode.Get(), g_opacity);
            MainUI::UpdateImGuiStyle();

            // ③ 改完 acrylic 再等 DWM 完成重排，期间不渲染
            std::this_thread::sleep_for(std::chrono::milliseconds(150));

            // ④ 若 DWM 重排把 device 弄丢了，这里主动恢复
#if PLATFORM_WINDOWS
            if (g_pd3dDevice && g_pd3dDevice->GetDeviceRemovedReason() != S_OK) {
                RecreateD3DDevice(g_hWnd);
            }
#endif

            continue;   // ★ 本帧不渲染，直接进入下一轮
        }



        AsyncEventManager::GetInstance().DrainMainThread();

        // 播放进度
        if (g_sm.IsPlaying() && ActiveBackend() && ActiveBackend()->IsLoaded())
            g_sm.UpdatePosition(ActiveBackend()->GetPositionMs());

        // 歌词滚动
        if (ActiveBackend() && ActiveBackend()->IsLoaded()) {
            int idx = PsCurrentLyricIndex();
            if (idx != g_currentLyricIndex) {
                g_currentLyricIndex = idx;
                g_needScrollToLyric = true;
            }
        }

        // 服务 Tick（每秒）
        if (++m_counter >= 60) m_counter = 0;
        if (m_counter == 0)
            PlaybackServiceRegistry::GetInstance().Tick(PsPositionMs(), PsIsPlaying());

        // 隐藏窗口降帧
        if (WindowAnimator::Get().IsHidden() && !WindowAnimator::Get().IsAnimating()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (!PlatformReadyToRender()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

#if PLATFORM_LINUX
        LnxPollEvents();
        LnxPollMouse();
        LnxPollX11Events();
        LnxBeginFrame();
#else
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
#endif

        MainUI::RenderFrame(m_counter, m_settings);

#if PLATFORM_WINDOWS
        const float clear[4] = { 0, 0, 0, 0 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            OutputDebugStringA("[D3D] Device removed on Present, recreating\n");
            RecreateD3DDevice(g_hWnd);
        }
#else
        LnxEndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
#endif


        if (m_quit) break;
    }

#if PLATFORM_WINDOWS
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_mainRenderTargetView) g_mainRenderTargetView->Release();
    if (g_pSwapChain)           g_pSwapChain->Release();
    if (g_pd3dDeviceContext)    g_pd3dDeviceContext->Release();
    if (g_pd3dDevice)           g_pd3dDevice->Release();

    CleanupWindowPlat();
#endif
    return 0;
}
