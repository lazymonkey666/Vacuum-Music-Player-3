/*
 * Vacuum Music Player 3  —  main.cpp
 * ------------------------------------------------------------------
 * 程序入口：窗口创建、主消息循环与应用生命周期调度。
 *
 * Version 3 (C++ rewrite)
 * Date    : 2026-04
 * Author  : lazymonkey666
 *           https://github.com/lazymonkey666
 *           https://lazymonkey666.github.io/
 * Repo    : https://github.com/lazymonkey666/Vacuum-Music-Player-3
 *
 * Copyright (c) 2026 lazymonkey666
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software, distributed WITHOUT ANY WARRANTY.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GPL along with this program
 * (see the LICENSE file); if not, see <https://www.gnu.org/licenses/>.
 */

#define MINIAUDIO_IMPLEMENTATION

#ifndef SPI_SETTHEMES
#define SPI_SETTHEMES 0x001A
#endif

#include <platform/platform.h>

#include "miniaudio.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdio.h>
#include <unordered_map>

// ImGui 核心
#include <imgui/imgui.h>

// 项目内部（逻辑层）
#include <AMLLWebSocketV2.h>
#include <ManageConfig.h>
#include <NeteaseAPIManager.h>
#include <SettingItem.h>
#include <TagReader.h>

#include <IconManager.h>
#include <ThemePack.h>

//模块化播放源
#include <MusicSource.h>

//模块化播放后端
#include <AsyncBackendBase.h>
#include <PlaybackBackend.h>

//模块化播放服务
#include <PlaybackService.h>

//异步事件管理器
#include <AsyncEventManager.h>

//加载sonos模块
#include <SonosService.h>

#include <PlaybackStateMachine.h>

#include <PlaybackView.h>

#include <AppState.h>
#include <initialize.h>
#include <MainUI.h>
#include <utils.h>

#include <WindowAnimator.h>
#include <application.h>


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

void PlayMusicFile(int index) {
    if (!g_source || index < 0 || index >= (int)g_source->GetCount()) return;

    // 1. UI 副作用（状态机不管）
    SetStatus(STATUS_FETCHING_URL);
    MainUI::g_scrollBlockCounter = 0;

    // 2. 预读标签（滚动条附近的 50 首）
    int totalCount = (int)g_source->GetCount();
    int startIdx = (index - 19 < 0) ? 0 : index - 19;
    int endIdx = (index + 30 > totalCount) ? totalCount : index + 30;
    g_source->RequestTrackDetails(startIdx, endIdx);

    // 3. 保存索引
    g_source->SaveIndex(index);

    // 4. 交给状态机（后面全自动）
    const BaseTrackInfo* track = g_source->GetBaseTrack(index);
    g_sm.Play(index, track ? track->id : "");
}

void UpdateCurrentPlaying(int new_index) {
    if (!g_source || g_source->GetCount() == 0) return;
    if (new_index < 0 || new_index >= (int)g_source->GetCount())
    {
        CurrentIndex = 0;
    }
    else
    {
        CurrentIndex = new_index;
    }
    PlayMusicFile(CurrentIndex);
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

int Destroy(HWND hWnd) {
    cfg.Save();
    WindowAnimator::Get().Stop();
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
    WindowAnimator::Get().Stop();
    std::filesystem::remove(coverPath);
    CleanupNetwork();
    UnregisterSessionNotification();
    UnregisterAllHotkeys();
    RequestQuitProgram();
    return 0;
}

bool ChangePlayMode() {
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
    case HOTKEY_SHOWHIDE:   WindowAnimator::Get().Toggle(); break;
    case HOTKEY_QUIT:       RequestQuit();       break;  // ← 见下方说明
    }
}

void ScheduleAlbumArtUpdate() {
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
        if (wParam == 12345 && WindowAnimator::Get().IsAnimating())
            WindowAnimator::Get().OnTick();
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
        g_needUpdateStyle = true;      // ★ 只设 flag
        break;
    }
    break;   // 必须 break，防止穿透
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        // 强调色改变时（Windows 10/11）
        g_needUpdateStyle = true;
        break;
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED) {
            if (g_pd3dDevice->GetDeviceRemovedReason() != S_OK) {
                RecreateD3DDevice(hWnd);
                break;
            }
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            if (width == 0 || height == 0) break;

            HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                RecreateD3DDevice(hWnd);
                break;
            }
            if (SUCCEEDED(hr)) {
                ID3D11Texture2D* pBackBuffer = nullptr;
                if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
                    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
                    pBackBuffer->Release();
                }
            }
        }
        break;



    case WM_RBUTTONDOWN: {
        POINT p; GetCursorPos(&p);
        WindowAnimator::Get().BeginDrag(p);
        CaptureMouse(hWnd);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (WindowAnimator::Get().IsDragging()) {
            POINT p; GetCursorPos(&p);
            WindowAnimator::Get().UpdateDrag(p);
            return 0;
        }
        break;
    }
    
    case WM_MOUSEWHEEL: {
        break;
    }
    case WM_RBUTTONUP:
    case WM_LBUTTONUP: {
        if (WindowAnimator::Get().IsDragging()) {
            WindowAnimator::Get().EndDrag();
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


void RegisterAsyncHooks() {
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
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return Application::Get().Run(0, nullptr);
}
#endif
#if PLATFORM_LINUX
bool LnxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (HandleAppMessage(msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SETTINGCHANGE:
        g_needUpdateStyle = true;
        return true;

    case WM_RBUTTONDOWN: {
        POINT p = GetMousePosPlat();
        WindowAnimator::Get().BeginDrag(p);
        CaptureMouse(hWnd);
        return true;
    }

    case WM_MOUSEMOVE: {
        if (WindowAnimator::Get().IsDragging()) {
            POINT p = GetMousePosPlat();
            WindowAnimator::Get().UpdateDrag(p);
            return true;
        }
        break;
    }

    case WM_MOUSEWHEEL:
        break;

    case WM_RBUTTONUP:
    case WM_LBUTTONUP:
        if (WindowAnimator::Get().IsDragging()) {
            WindowAnimator::Get().EndDrag();
            return true;
        }
        break;

    case WM_CLOSE:
        Destroy(hWnd);
        return true;
    }
    return false;
}
#endif

#if PLATFORM_LINUX
int main(int argc, char** argv) {
    return Application::Get().Run(argc, argv);
}
#endif