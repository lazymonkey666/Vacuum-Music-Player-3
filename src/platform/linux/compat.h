// platform/linux/win_compat.h
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <chrono>

// ============================================================
// 1. 基础类型
// ============================================================
using BOOL = int;
using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = unsigned int;
using ULONG = unsigned long;
using LONG = long;
using ULONGLONG = unsigned long long;
using SIZE_T = size_t;
using UINT_PTR = uintptr_t;
using INT_PTR = intptr_t;
using LRESULT = intptr_t;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using HRESULT = long;

using LPCSTR = const char*;
using LPSTR = char*;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
using LPVOID = void*;
using PVOID = void*;
using COLORREF = DWORD;

// 前置声明（GLFW 窗口类型，只做前向声明，避免头文件依赖 GLFW）
struct GLFWwindow;

// 前置声明（内部窗口对象）
struct PlatformWindow;

// 窗口句柄
using HWND = PlatformWindow*;
using HINSTANCE = void*;
using HMODULE = void*;
using HICON = void*;
using HANDLE = void*;
using HKEY = void*;

struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };

// ============================================================
// 2. 消息结构（在 PlatformWindow 之前）
// ============================================================
struct MSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
};

// ============================================================
// 3. WndProc 函数指针类型
// ============================================================
using WndProcFn = LRESULT(*)(HWND, UINT, WPARAM, LPARAM);

// ============================================================
// 4. PlatformWindow 完整定义
//
//    main.cpp 需要直接访问 ->glfw 字段时，就靠这份定义。
//    GLFWwindow 只做前向声明，指针大小固定。
// ============================================================
struct PlatformWindow {
    GLFWwindow* glfw = nullptr;
    WndProcFn   wndProc = nullptr;

    // 假消息队列
    std::mutex mu;
    std::queue<MSG> queue;

    // 周期定时器
    struct TimerEntry {
        UINT_PTR id;
        UINT     intervalMs;
        std::chrono::steady_clock::time_point nextFire;
    };
    std::mutex timerMu;
    std::vector<TimerEntry> timers;
    UINT_PTR nextTimerId = 1;

    // 鼠标边界检测
    bool lastLeftDown = false;
    bool lastRightDown = false;
    bool capturing = false;

    // 热键表
    struct HotkeyEntry { int id; UINT mods; UINT vk; };
    std::vector<HotkeyEntry> hotkeys;
};

// ============================================================
// 5. 常量
// ============================================================
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL nullptr
#endif

constexpr DWORD ERROR_SUCCESS = 0;
constexpr DWORD ERROR_ALREADY_EXISTS = 183;
constexpr DWORD MAX_PATH = 260;

constexpr HRESULT S_OK = 0;
constexpr HRESULT E_FAIL = (HRESULT)0x80004005L;
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

// 消息框
#define MB_OK               0x00000000L
#define MB_OKCANCEL         0x00000001L
#define MB_ICONHAND         0x00000010L
#define MB_ICONQUESTION     0x00000020L
#define MB_ICONEXCLAMATION  0x00000030L
#define MB_ICONASTERISK     0x00000040L
#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_ICONERROR        MB_ICONHAND
#define IDOK                1
#define IDCANCEL            2

// SetWindowPos
#define SWP_NOSIZE          0x0001
#define SWP_NOMOVE          0x0002
#define SWP_NOZORDER        0x0004
#define SWP_NOACTIVATE      0x0010
#define SWP_SHOWWINDOW      0x0040
#define SWP_HIDEWINDOW      0x0080
#define SWP_NOOWNERZORDER   0x0200
#define SWP_NOSENDCHANGING  0x0400

#define HWND_TOP            ((HWND)0)
#define HWND_BOTTOM         ((HWND)1)
#define HWND_TOPMOST        ((HWND)-1)
#define HWND_NOTOPMOST      ((HWND)-2)

#define LWA_COLORKEY        0x00000001
#define LWA_ALPHA           0x00000002

#define SM_CXSCREEN         0
#define SM_CYSCREEN         1
#define SPI_GETWORKAREA     0x0030
#ifndef SPI_SETTHEMES
#define SPI_SETTHEMES       0x001A
#endif

#define CP_UTF8             65001
#define CP_ACP              0
#define CP_UTF16            1200

#define MOD_ALT             0x0001
#define MOD_CONTROL         0x0002
#define MOD_SHIFT           0x0004
#define MOD_WIN             0x0008

#define VK_OEM_PERIOD       0xBE
#define VK_OEM_COMMA        0xBC
#define VK_OEM_2            0xBF

#define ICON_SMALL          0
#define ICON_BIG            1

#define HKEY_CURRENT_USER   ((HKEY)(uintptr_t)0x80000001)
#define KEY_READ            0x20019

#define PM_NOREMOVE         0x0000
#define PM_REMOVE           0x0001

// WM_* 消息
#define WM_NULL             0x0000
#define WM_CREATE           0x0001
#define WM_DESTROY          0x0002
#define WM_SIZE             0x0005
#define WM_CLOSE            0x0010
#define WM_QUIT             0x0012
#define WM_SETICON          0x0080
#define WM_SETTINGCHANGE    0x001A
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#define WM_TIMER            0x0113
#define WM_HOTKEY           0x0312
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_MOUSEWHEEL       0x020A
#define WM_USER             0x0400
#define WM_APP              0x8000

// 窗口样式（Linux 侧仅作常量）
#define WS_POPUP            0x80000000L
#define WS_VISIBLE          0x10000000L
#define WS_EX_LAYERED       0x00080000L
#define WS_EX_APPWINDOW     0x00040000L
#define WS_EX_TOPMOST       0x00000008L

#define SIZE_MINIMIZED      1

// ============================================================
// 6. 函数声明
// ============================================================
// 调试输出
void OutputDebugStringA(LPCSTR s);
void OutputDebugStringW(LPCWSTR s);

// 消息框
int MessageBoxA(HWND hWnd, LPCSTR text, LPCSTR caption, UINT type);
int MessageBoxW(HWND hWnd, LPCWSTR text, LPCWSTR caption, UINT type);
#ifdef UNICODE
#define MessageBox MessageBoxW
#else
#define MessageBox MessageBoxA
#endif

// 睡眠/计时
void       Sleep(DWORD ms);
DWORD      GetTickCount();
ULONGLONG  GetTickCount64();

// 编码转换
int MultiByteToWideChar(UINT CodePage, DWORD dwFlags,
    LPCSTR lpMultiByteStr, int cbMultiByte,
    LPWSTR lpWideCharStr, int cchWideChar);
int WideCharToMultiByte(UINT CodePage, DWORD dwFlags,
    LPCWSTR lpWideCharStr, int cchWideChar,
    LPSTR lpMultiByteStr, int cbMultiByte,
    LPCSTR lpDefaultChar, BOOL* lpUsedDefaultChar);
std::string  WStringToUtf8(const std::wstring& s);
std::wstring Utf8ToWString(const std::string& s);
std::wstring s2ws(const std::string& str);

// 文件
BOOL DeleteFileA(LPCSTR path);
BOOL DeleteFileW(LPCWSTR path);
#ifdef UNICODE
#define DeleteFile DeleteFileW
#else
#define DeleteFile DeleteFileA
#endif

DWORD GetTempPathA(DWORD n, LPSTR buf);
DWORD GetTempPathW(DWORD n, LPWSTR buf);
#ifdef UNICODE
#define GetTempPath GetTempPathW
#else
#define GetTempPath GetTempPathA
#endif

int _wfopen_s(std::FILE** pFile, const wchar_t* filename, const wchar_t* mode);
int fopen_s(std::FILE** pFile, const char* filename, const char* mode);

// 窗口操作
BOOL GetWindowRect(HWND hWnd, RECT* rect);
BOOL GetClientRect(HWND hWnd, RECT* rect);
BOOL SetWindowPos(HWND hWnd, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL SetLayeredWindowAttributes(HWND hWnd, COLORREF key, BYTE alpha, DWORD flags);
BOOL GetCursorPos(POINT* pt);
BOOL SetCursorPos(int x, int y);
BOOL SystemParametersInfoA(UINT action, UINT param, PVOID data, UINT winIni);
#ifndef SystemParametersInfo
#define SystemParametersInfo SystemParametersInfoA
#endif
int  GetSystemMetrics(int index);

UINT_PTR SetTimer(HWND hWnd, UINT_PTR id, UINT elapse, void* fn);
BOOL     KillTimer(HWND hWnd, UINT_PTR id);

HWND SetCapture(HWND hWnd);
BOOL ReleaseCapture();
BOOL InvalidateRect(HWND hWnd, const RECT* rect, BOOL erase);

HICON   LoadIconA(HINSTANCE h, LPCSTR name);
HICON   LoadIconW(HINSTANCE h, LPCWSTR name);
#ifdef UNICODE
#define LoadIcon LoadIconW
#else
#define LoadIcon LoadIconA
#endif

HMODULE GetModuleHandleA(LPCSTR name);
HMODULE GetModuleHandleW(LPCWSTR name);
#ifdef UNICODE
#define GetModuleHandle GetModuleHandleW
#else
#define GetModuleHandle GetModuleHandleA
#endif

// 消息队列
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#ifdef UNICODE
#define PostMessage PostMessageW
#else
#define PostMessage PostMessageA
#endif

LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#ifdef UNICODE
#define SendMessage SendMessageW
#else
#define SendMessage SendMessageA
#endif

void PostQuitMessage(int code);

BOOL PeekMessageA(MSG* msg, HWND hWnd, UINT min, UINT max, UINT removeMsg);
#ifndef PeekMessage
#define PeekMessage PeekMessageA
#endif

BOOL    TranslateMessage(const MSG* msg);
LRESULT DispatchMessageA(const MSG* msg);
#ifndef DispatchMessage
#define DispatchMessage DispatchMessageA
#endif

// 注册表
LONG RegOpenKeyExW(HKEY key, LPCWSTR subkey, DWORD options,
    DWORD desired, HKEY* result);
LONG RegQueryValueExW(HKEY key, LPCWSTR valueName, DWORD* reserved,
    DWORD* type, BYTE* data, DWORD* dataSize);
LONG RegCloseKey(HKEY key);

// 热键
BOOL RegisterHotKey(HWND hWnd, int id, UINT mods, UINT vk);
BOOL UnregisterHotKey(HWND hWnd, int id);

// 网络
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int    SOCKET_ERROR = -1;

struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
};

int WSAStartup(WORD version, WSADATA* data);
int WSACleanup();
int closesocket(SOCKET s);
int InetPtonA(int family, LPCSTR src, PVOID dst);

// 其他
HRESULT SetCurrentProcessExplicitAppUserModelID(LPCWSTR appId);

// ============================================================
// 7. 工厂函数（在 platform/linux/win_compat.cpp 里实现）
// ============================================================
HWND platform_CreateGlfwWindow(int width, int height, const char* title);
void platform_DestroyGlfwWindow(HWND w);
bool platform_WindowShouldClose(HWND w);
void platform_SwapBuffers(HWND w);
void platform_PumpEvents(HWND w);

// ============================================================
// 8. platform_compat 命名空间（提供 WndProc 注册等）
// ============================================================
namespace platform_compat {

    void SetCurrentWindow(PlatformWindow* w);
    PlatformWindow* GetCurrentWindow();
    void SetWndProc(PlatformWindow* w, WndProcFn fn);
    void ProcessEvents();

}  // namespace platform_compat