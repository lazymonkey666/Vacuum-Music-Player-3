#pragma once

// ═══════════════════════════════════════════════════════════
// 0. 平台判定（全项目唯一一处用编译器内置宏）
// ═══════════════════════════════════════════════════════════
#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
#define PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#define PLATFORM_MACOS   1
#elif defined(__linux__)
#define PLATFORM_LINUX   1
#else
#error "Unsupported platform"
#endif

// ═══════════════════════════════════════════════════════════
// 1. 通用宏（不依赖平台）
// ═══════════════════════════════════════════════════════════
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef SPI_SETTHEMES
#define SPI_SETTHEMES 0x001A
#endif

// ═══════════════════════════════════════════════════════════
// 2. Windows 专属宏（必须在任何系统头文件之前定义）
// ═══════════════════════════════════════════════════════════
#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif

// ═══════════════════════════════════════════════════════════
// 3. Windows 系统头文件（顺序很重要，不要随意调换）
// ═══════════════════════════════════════════════════════════
#if PLATFORM_WINDOWS

// 3.1 Winsock 必须在 windows.h 之前（Windows 头文件的经典坑）
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

// 3.2 Windows 基础
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>

// 3.3 COM / Shell（ITaskbarList3、SetCurrentProcessExplicitAppUserModelID 在这）
#include <ShObjIdl.h>
#include <shlobj.h>

// 3.4 图片 / 网络 / 会话
#include <wincodec.h>
#include <urlmon.h>
#include <wtsapi32.h>

// 3.5 DirectX
#include <dxgi.h>
#include <d3d11.h>

// 3.6 WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Storage.Streams.h>

// 3.7 ImGui 后端
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// 3.8 资源
#include "resource.h"

// 3.9 链接库
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "Version.lib")


#endif // PLATFORM_WINDOWS

// ═══════════════════════════════════════════════════════════
// 4. 跨平台标准库
// ═══════════════════════════════════════════════════════════
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <future>

// ═══════════════════════════════════════════════════════════
// 5. 平台类型别名
// ═══════════════════════════════════════════════════════════
#if PLATFORM_WINDOWS

using TextureHandle = ID3D11ShaderResourceView*;
using WindowHandle = HWND;
using PathType = std::wstring;

#else  // 非 Windows：垫出所有 Win32 类型

// 5.1 句柄/整数类型
using HWND = void*;
using HINSTANCE = void*;
using HANDLE = void*;
using HMODULE = void*;
using HICON = void*;
using LRESULT = long;
using WPARAM = unsigned long long;
using LPARAM = long long;
using UINT = unsigned int;
using DWORD = uint32_t;
using BYTE = uint8_t;
using WORD = uint16_t;
using UINT_PTR = uintptr_t;
using COLORREF = uint32_t;

// 5.2 类型别名
using TextureHandle = void*;
using WindowHandle = void*;
using PathType = std::string;

// 5.3 结构体
struct RECT { int left, top, right, bottom; };
struct POINT { long x, y; };
struct MSG { UINT message; WPARAM wParam; LPARAM lParam; };

// 5.4 调用约定
#define CALLBACK
#define WINAPI

#endif // PLATFORM_WINDOWS

// ═══════════════════════════════════════════════════════════
// 6. 常量垫层（非 Windows 平台）
// ═══════════════════════════════════════════════════════════
#if !PLATFORM_WINDOWS

// ─── 系统度量 ───
#define SM_CXSCREEN  0
#define SM_CYSCREEN  1

// ─── SystemParametersInfo ───
#define SPI_GETWORKAREA  0x0030

// ─── SetWindowPos flags ───
#define HWND_TOPMOST         ((HWND)-1)
#define HWND_NOTOPMOST       ((HWND)-2)
#define SWP_NOSIZE           0x0001
#define SWP_NOMOVE           0x0002
#define SWP_NOZORDER         0x0004
#define SWP_NOSENDCHANGING   0x0400

// ─── SetLayeredWindowAttributes ───
#define LWA_ALPHA  0x00000002

// ─── MessageBox ───
#define MB_OK                0x00000000
#define MB_OKCANCEL          0x00000001
#define MB_ICONINFORMATION   0x00000040
#define MB_ICONERROR         0x00000010
#define IDOK                 1

// ─── WM_* 消息 ───
#define WM_CREATE                       0x0001
#define WM_DESTROY                      0x0002
#define WM_SIZE                         0x0005
#define WM_TIMER                        0x0113
#define WM_HOTKEY                       0x0312
#define WM_MOUSEMOVE                    0x0200
#define WM_LBUTTONUP                    0x0202
#define WM_RBUTTONDOWN                  0x0204
#define WM_RBUTTONUP                    0x0205
#define WM_MOUSEWHEEL                   0x020A
#define WM_SETTINGCHANGE                0x001A
#define WM_DWMCOLORIZATIONCOLORCHANGED  0x0320
#define WM_SETICON                      0x0080

// ─── WM_SIZE / WM_SETICON 参数 ───
#define SIZE_MINIMIZED  1
#define ICON_BIG        1
#define ICON_SMALL      0

// ─── 会话通知 ───
#define NOTIFY_FOR_THIS_SESSION  0

// ─── socket ───
#define AF_INET        2
#define SOCK_STREAM    1
#define IPPROTO_TCP    6
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
typedef int SOCKET;

// ─── 其他 ───
#define CP_UTF8   65001
#define MAX_PATH  260

#define closesocket close

#endif // !PLATFORM_WINDOWS

// ═══════════════════════════════════════════════════════════
// 7. 自定义消息 ID
// ═══════════════════════════════════════════════════════════
#if !PLATFORM_WINDOWS
#define WM_APP 0x8000
#endif

#define HOTKEY_NEXT              1001
#define HOTKEY_PREV              1002
#define HOTKEY_PLAYPAUSE         1003
#define HOTKEY_SHOWHIDE          1004
#define HOTKEY_QUIT              1005

#define WM_APP_SMTC_PLAY         (WM_APP + 1)
#define WM_APP_SMTC_PAUSE        (WM_APP + 2)
#define WM_APP_SMTC_NEXT         (WM_APP + 3)
#define WM_APP_SMTC_PREV         (WM_APP + 4)
#define WM_UPDATE_ALBUM_ART      (WM_APP + 5)
#define WM_USER_SOFTWARE_RESTART (WM_APP + 100)
#define WM_PLAY_READY            (WM_APP + 200)
#define WM_PLAY_FAILED           (WM_APP + 201)
#define WM_SONOS_STOPPED         (WM_APP + 300)




// ═══════════════════════════════════════════════════════════
// 8. 全局句柄
// ═══════════════════════════════════════════════════════════
extern WindowHandle g_hWnd;

#if PLATFORM_WINDOWS
extern HANDLE g_hMutex;
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
extern IDXGISwapChain* g_pSwapChain;
extern ID3D11RenderTargetView* g_mainRenderTargetView;
#endif

// ═══════════════════════════════════════════════════════════
// 9. 播放状态枚举
// ═══════════════════════════════════════════════════════════
enum class PlaybackStatus { Playing, Paused, Stopped };

// ═══════════════════════════════════════════════════════════
// 10. 平台函数声明
// ═══════════════════════════════════════════════════════════

// ─── 窗口外观 ───
void     EnableAcrylic(WindowHandle hWnd, uint8_t opacity, uint32_t color);
bool     IsDarkModeByRegistry();
uint32_t GetAccentColorFromRegistry();

// ─── D3D11 ───
bool          CreateDeviceD3D(WindowHandle hWnd);
TextureHandle CreateTextureFromImageData(const std::vector<unsigned char>& imageData);
TextureHandle CreateTextureFromRGBA(const std::vector<unsigned char>& rgbaData, int width, int height);

// ─── 媒体控制（SMTC）───
void InitSMTC(WindowHandle hWnd);
void SetSMTCPlaybackStatus(PlaybackStatus status);
void UpdateSMTC(const std::string& title,
    const std::string& artist,
    const std::string& album,
    const std::vector<unsigned char>& cover);

// ─── 任务栏进度 ───
bool InitTaskbarProgress(WindowHandle hWnd);
void SetTaskbarProgress(uint64_t current, uint64_t total);
void SetTaskbarState(bool playing);
bool HandleTaskbarMessage(unsigned int msg);

// ─── 热键 ───
void InitHotkey(WindowHandle hWnd);

// ─── 其他 ───
void     RequestQuit();
PathType Utf8ToPath(const std::string& utf8);
std::string PathToUtf8(const PathType& path);

int GetScreenWidth();
int GetScreenHeight();

RECT GetWindowRectPlat(WindowHandle hWnd);

uintptr_t StartWindowTimer(WindowHandle hWnd, int intervalMs);
void      StopWindowTimer(WindowHandle hWnd, uintptr_t timerId);

std::string GetTempDir();

#if PLATFORM_WINDOWS
#define MA_DECODER_INIT_FILE     ma_decoder_init_file_w
#define MA_SOUND_INIT_FROM_FILE  ma_sound_init_from_file_w
#else
#define MA_DECODER_INIT_FILE     ma_decoder_init_file
#define MA_SOUND_INIT_FROM_FILE  ma_sound_init_from_file
#endif

bool DownloadFile(const std::string& url, const std::string& localPath);

std::string ReadTextFile(const PathType& path);

void SetWindowAlpha(WindowHandle hWnd, uint8_t alpha);
void MoveWindowPlat(WindowHandle hWnd, int x, int y);              // 只移动
void MoveResizeWindowPlat(WindowHandle hWnd, int x, int y, int w, int h);  // 移动+改大小

int FindAvailablePort(int minPort, int maxPort);

void ReleaseTexture(TextureHandle tex);
void UnregisterSessionNotification();
void UnregisterAllHotkeys();
void RequestQuitProgram();

bool InitNetwork();//先暂时用不到 因为在WinMain里面，本来就是和平台有关的函数
void CleanupNetwork();

void ShowMessageBox(const std::string& title, const std::string& message);
bool ShowConfirmBox(const std::string& title, const std::string& message);

void  CaptureMouse(WindowHandle hWnd);
void  ReleaseMouseCapture();
POINT GetMousePosPlat();

// ─── 密钥存储 ───
bool EncryptString(const std::string& plain, std::string& encrypted);
bool DecryptString(const std::string& encrypted, std::string& plain);

bool InitWindowPlat(const std::string& className);
void CleanupWindowPlat();
RECT GetWorkAreaPlat();

// ─── 纹理加载 ───
TextureHandle LoadTextureFromResource(int resourceId,
    const wchar_t* resourceType,
    int* out_width = nullptr,
    int* out_height = nullptr);

void OpenUrlInBrowser(const std::string& url);

std::vector<unsigned char> DecodeAndScaleImage(const std::vector<unsigned char>& imageData, int targetWidth, int targetHeight);
std::vector<unsigned char> ConvertImageToJPEG(const std::vector<unsigned char>& inputImageData);

std::string GetFileVersionString();
// ─── 纹理加载 ───
TextureHandle LoadAppIcon(int* out_width = nullptr, int* out_height = nullptr);
void RestartApplication();
void SaveAndRestart();


// ═══════════════════════════════════════════════════════════
// 网络适配器枚举
// ═══════════════════════════════════════════════════════════
struct NetAdapterInfo {
    std::string ip;            // "192.168.1.100"
    std::string description;   // "Intel(R) Wi-Fi 6 AX201 160MHz"
    bool        hasGateway = false;
    int         type = 0;      // 0=unknown, 1=ethernet, 2=wifi
};

std::vector<NetAdapterInfo> EnumNetworkAdapters();



// ─── WndProc（仅 Windows）───
#if PLATFORM_WINDOWS
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
#endif