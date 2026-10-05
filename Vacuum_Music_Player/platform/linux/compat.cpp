// platform/linux/compat.cpp
#include "compat.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <string>
#include <algorithm>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctime>

#include <GLFW/glfw3.h>

#if defined(__linux__) && !defined(__WAYLAND__)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#define HAS_X11 1
#else
#define HAS_X11 0
#endif

// ============================================================
// 内部辅助
// ============================================================
namespace {

    PlatformWindow* g_currentWindow = nullptr;

    // UTF-32 <-> UTF-8
    std::string Utf32ToUtf8(const wchar_t* wstr) {
        if (!wstr) return {};
        std::string out;
        for (const wchar_t* p = wstr; *p; ++p) {
            uint32_t cp = static_cast<uint32_t>(*p);
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    std::wstring Utf8ToUtf32(const char* s, int len = -1) {
        if (!s) return {};
        std::wstring out;
        size_t i = 0;
        while (s[i] != '\0' && (len < 0 || (int)i < len)) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            uint32_t cp = 0;
            int extra = 0;
            if (c < 0x80) { cp = c;        extra = 0; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
            else { ++i; continue; }
            for (int k = 0; k < extra; ++k) {
                if (!s[i + 1]) break;
                ++i;
                cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
            }
            out.push_back(static_cast<wchar_t>(cp));
            ++i;
        }
        return out;
    }

    bool HasZenity() {
        static const bool v = (std::system("which zenity > /dev/null 2>&1") == 0);
        return v;
    }

    std::string EscapeShell(const std::string& s) {
        std::string r;
        r.reserve(s.size());
        for (char c : s) {
            if (c == '"')       r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '$')  r += "\\$";
            else if (c == '`')  r += "\\`";
            else                r += c;
        }
        return r;
    }

    // 把当前鼠标状态转成消息
    void PostMouseButtonEvents(PlatformWindow* w) {
        if (!w || !w->glfw) return;

        bool leftDown = glfwGetMouseButton(w->glfw, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rightDown = glfwGetMouseButton(w->glfw, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        double x = 0, y = 0;
        glfwGetCursorPos(w->glfw, &x, &y);
        LPARAM lParam = ((LPARAM)(uint16_t)(int)x) |
            (((LPARAM)(uint16_t)(int)y) << 16);

        if (leftDown != w->lastLeftDown) {
            UINT msg = leftDown ? WM_LBUTTONDOWN : WM_LBUTTONUP;
            PostMessageA(w, msg, (WPARAM)0, lParam);
            w->lastLeftDown = leftDown;
        }
        if (rightDown != w->lastRightDown) {
            UINT msg = rightDown ? WM_RBUTTONDOWN : WM_RBUTTONUP;
            PostMessageA(w, msg, (WPARAM)0, lParam);
            w->lastRightDown = rightDown;
        }
    }

    // 周期定时器
    void ProcessTimers(PlatformWindow* w) {
        if (!w) return;

        auto now = std::chrono::steady_clock::now();
        std::vector<UINT_PTR> fired;

        {
            std::lock_guard<std::mutex> lk(w->timerMu);
            for (auto& t : w->timers) {
                if (now >= t.nextFire) {
                    fired.push_back(t.id);
                    t.nextFire = now + std::chrono::milliseconds(t.intervalMs);
                }
            }
        }

        for (auto id : fired) {
            if (w->wndProc) {
                w->wndProc(w, WM_TIMER, (WPARAM)id, 0);
            }
        }
    }

}  // namespace

// ============================================================
// 调试输出
// ============================================================
void OutputDebugStringA(LPCSTR s) {
    if (!s) return;
    std::fprintf(stderr, "%s", s);
    std::fflush(stderr);
}

void OutputDebugStringW(LPCWSTR s) {
    if (!s) return;
    std::string u8 = Utf32ToUtf8(s);
    std::fprintf(stderr, "%s", u8.c_str());
    std::fflush(stderr);
}

// ============================================================
// 消息框
// ============================================================
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT) {
    std::string cap = caption ? caption : "";
    std::string txt = text ? text : "";
    std::fprintf(stderr, "[MessageBox] %s: %s\n", cap.c_str(), txt.c_str());
    if (HasZenity()) {
        std::string cmd = "zenity --info --title=\"" + EscapeShell(cap) +
            "\" --text=\"" + EscapeShell(txt) + "\" 2>/dev/null";
        std::system(cmd.c_str());
    }
    return IDOK;
}

int MessageBoxW(HWND h, LPCWSTR text, LPCWSTR caption, UINT type) {
    std::string cap = Utf32ToUtf8(caption);
    std::string txt = Utf32ToUtf8(text);
    return MessageBoxA(h, txt.c_str(), cap.c_str(), type);
}

// ============================================================
// 睡眠 / 计时
// ============================================================
void Sleep(DWORD ms) {
    if (ms == 0) std::this_thread::yield();
    else std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

DWORD GetTickCount() {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return static_cast<DWORD>(duration_cast<milliseconds>(now).count());
}

ULONGLONG GetTickCount64() {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return static_cast<ULONGLONG>(duration_cast<milliseconds>(now).count());
}

// ============================================================
// 编码转换
// ============================================================
int MultiByteToWideChar(UINT CodePage, DWORD,
    LPCSTR src, int srcLen,
    LPWSTR dst, int dstLen) {
    if (!src) return 0;

    std::wstring w;
    if (CodePage == CP_UTF8 || CodePage == CP_ACP) {
        w = Utf8ToUtf32(src, srcLen);
    }
    else {
        int n = (srcLen < 0) ? (int)std::strlen(src) : srcLen;
        w.assign(src, src + n);
    }
    if (srcLen < 0) w.push_back(L'\0');

    int need = (int)w.size();
    if (dstLen == 0) return need;
    int n = (need < dstLen) ? need : dstLen;
    for (int i = 0; i < n; ++i) dst[i] = w[i];
    return n;
}

int WideCharToMultiByte(UINT CodePage, DWORD,
    LPCWSTR src, int srcLen,
    LPSTR dst, int dstLen,
    LPCSTR, BOOL*) {
    if (!src) return 0;

    std::string s;
    if (CodePage == CP_UTF8 || CodePage == CP_ACP) {
        if (srcLen < 0) {
            s = Utf32ToUtf8(src);
        }
        else {
            std::wstring tmp(src, src + srcLen);
            s = Utf32ToUtf8(tmp.c_str());
        }
    }
    else {
        int n = (srcLen < 0) ? (int)std::wcslen(src) : srcLen;
        s.assign(reinterpret_cast<const char*>(src), n);
    }

    int need = (int)s.size() + 1;
    if (dstLen == 0) return need;
    int n = (need < dstLen) ? need : dstLen;
    std::memcpy(dst, s.c_str(), n);
    return n;
}

std::string  WStringToUtf8(const std::wstring& s) { return Utf32ToUtf8(s.c_str()); }
std::wstring Utf8ToWString(const std::string& s) { return Utf8ToUtf32(s.c_str()); }

std::wstring s2ws(const std::string& str) {
    if (str.empty()) return {};
    return Utf8ToUtf32(str.c_str());
}

// ============================================================
// 文件
// ============================================================
BOOL DeleteFileA(LPCSTR path) {
    if (!path) return FALSE;
    return (::unlink(path) == 0) ? TRUE : FALSE;
}

BOOL DeleteFileW(LPCWSTR path) {
    if (!path) return FALSE;
    return DeleteFileA(Utf32ToUtf8(path).c_str());
}

DWORD GetTempPathA(DWORD n, LPSTR buf) {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::string dir;
    if (xdg && *xdg) {
        dir = xdg;
    }
    else {
        const char* home = std::getenv("HOME");
        dir = home ? std::string(home) + "/.cache" : "/tmp";
    }
    if (dir.empty() || dir.back() != '/') dir += '/';

    if (!buf || n == 0) return (DWORD)dir.size();
    std::strncpy(buf, dir.c_str(), n);
    buf[n - 1] = '\0';
    return (DWORD)std::strlen(buf);
}

DWORD GetTempPathW(DWORD n, LPWSTR buf) {
    char tmp[MAX_PATH + 1] = { 0 };
    GetTempPathA(sizeof(tmp), tmp);
    std::wstring w = Utf8ToUtf32(tmp);
    if (!buf || n == 0) return (DWORD)w.size();
    DWORD copy = ((DWORD)w.size() < n) ? (DWORD)w.size() : n - 1;
    std::wmemcpy(buf, w.c_str(), copy);
    buf[copy] = L'\0';
    return copy;
}

int _wfopen_s(std::FILE** pFile, const wchar_t* filename, const wchar_t* mode) {
    if (!pFile || !filename || !mode) return -1;
    std::string fn = Utf32ToUtf8(filename);
    std::string md = Utf32ToUtf8(mode);
    *pFile = std::fopen(fn.c_str(), md.c_str());
    return *pFile ? 0 : -1;
}

int fopen_s(std::FILE** pFile, const char* filename, const char* mode) {
    if (!pFile || !filename || !mode) return -1;
    *pFile = std::fopen(filename, mode);
    return *pFile ? 0 : -1;
}

// ============================================================
// 窗口操作
// ============================================================
BOOL GetWindowRect(HWND hWnd, RECT* rect) {
    if (!hWnd || !rect || !hWnd->glfw) return FALSE;
    int x, y, w, h;
    glfwGetWindowPos(hWnd->glfw, &x, &y);
    glfwGetWindowSize(hWnd->glfw, &w, &h);
    rect->left = x;
    rect->top = y;
    rect->right = x + w;
    rect->bottom = y + h;
    return TRUE;
}

BOOL GetClientRect(HWND hWnd, RECT* rect) {
    if (!hWnd || !rect || !hWnd->glfw) return FALSE;
    int w, h;
    glfwGetFramebufferSize(hWnd->glfw, &w, &h);
    rect->left = 0; rect->top = 0;
    rect->right = w; rect->bottom = h;
    return TRUE;
}

BOOL SetWindowPos(HWND hWnd, HWND, int x, int y,
    int cx, int cy, UINT flags) {
    if (!hWnd || !hWnd->glfw) return FALSE;

    if (!(flags & SWP_NOMOVE)) {
        glfwSetWindowPos(hWnd->glfw, x, y);
    }
    if (!(flags & SWP_NOSIZE) && cx > 0 && cy > 0) {
        glfwSetWindowSize(hWnd->glfw, cx, cy);
    }
    glfwSetWindowAttrib(hWnd->glfw, GLFW_FLOATING, GLFW_TRUE);
    return TRUE;
}

BOOL SetLayeredWindowAttributes(HWND hWnd, COLORREF,
    BYTE alpha, DWORD) {
    if (!hWnd || !hWnd->glfw) return FALSE;
    glfwSetWindowOpacity(hWnd->glfw, alpha / 255.0f);
    return TRUE;
}

BOOL GetCursorPos(POINT* pt) {
    if (!pt) return FALSE;
    PlatformWindow* w = g_currentWindow;
    if (!w || !w->glfw) { pt->x = 0; pt->y = 0; return FALSE; }

    double cx, cy;
    glfwGetCursorPos(w->glfw, &cx, &cy);
    int wx, wy;
    glfwGetWindowPos(w->glfw, &wx, &wy);
    pt->x = wx + (LONG)cx;
    pt->y = wy + (LONG)cy;
    return TRUE;
}

BOOL SetCursorPos(int x, int y) {
    PlatformWindow* w = g_currentWindow;
    if (!w || !w->glfw) return FALSE;

    int wx, wy;
    glfwGetWindowPos(w->glfw, &wx, &wy);
    glfwSetCursorPos(w->glfw, x - wx, y - wy);
    return TRUE;
}

BOOL SystemParametersInfoA(UINT action, UINT,
    PVOID data, UINT) {
    if (action != SPI_GETWORKAREA || !data) return FALSE;
    RECT* r = static_cast<RECT*>(data);

    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (!mon) {
        r->left = 0; r->top = 0; r->right = 1920; r->bottom = 1080;
        return TRUE;
    }
    int mx, my, mw, mh;
    glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
    r->left = mx; r->top = my;
    r->right = mx + mw; r->bottom = my + mh;
    return TRUE;
}

int GetSystemMetrics(int index) {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    switch (index) {
    case SM_CXSCREEN: return vm ? vm->width : 1920;
    case SM_CYSCREEN: return vm ? vm->height : 1080;
    default: return 0;
    }
}

UINT_PTR SetTimer(HWND hWnd, UINT_PTR id, UINT elapse, void*) {
    if (!hWnd) return 0;
    std::lock_guard<std::mutex> lk(hWnd->timerMu);

    UINT_PTR useId = id ? id : hWnd->nextTimerId++;

    for (auto& t : hWnd->timers) {
        if (t.id == useId) {
            t.intervalMs = elapse ? elapse : 1;
            t.nextFire = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(t.intervalMs);
            return useId;
        }
    }
    PlatformWindow::TimerEntry e;
    e.id = useId;
    e.intervalMs = elapse ? elapse : 1;
    e.nextFire = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(e.intervalMs);
    hWnd->timers.push_back(e);
    return useId;
}

BOOL KillTimer(HWND hWnd, UINT_PTR id) {
    if (!hWnd) return FALSE;
    std::lock_guard<std::mutex> lk(hWnd->timerMu);
    auto& v = hWnd->timers;
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->id == id) { v.erase(it); return TRUE; }
    }
    return FALSE;
}

HWND SetCapture(HWND hWnd) {
    if (hWnd) hWnd->capturing = true;
    return hWnd;
}

BOOL ReleaseCapture() {
    if (g_currentWindow) g_currentWindow->capturing = false;
    return TRUE;
}

BOOL InvalidateRect(HWND, const RECT*, BOOL) { return TRUE; }

HICON LoadIconA(HINSTANCE, LPCSTR) { return nullptr; }
HICON LoadIconW(HINSTANCE, LPCWSTR) { return nullptr; }

HMODULE GetModuleHandleA(LPCSTR) { return nullptr; }
HMODULE GetModuleHandleW(LPCWSTR) { return nullptr; }

// ============================================================
// 消息队列
// ============================================================
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (!hWnd) return FALSE;
    std::lock_guard<std::mutex> lk(hWnd->mu);
    MSG m{};
    m.hwnd = hWnd;
    m.message = Msg;
    m.wParam = wParam;
    m.lParam = lParam;
    m.time = GetTickCount();
    hWnd->queue.push(m);
    return TRUE;
}

BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l) {
    return PostMessageA(h, m, w, l);
}

LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (!hWnd || !hWnd->wndProc) return 0;
    return hWnd->wndProc(hWnd, Msg, wParam, lParam);
}

LRESULT SendMessageW(HWND h, UINT m, WPARAM w, LPARAM l) {
    return SendMessageA(h, m, w, l);
}

void PostQuitMessage(int) {
    if (g_currentWindow && g_currentWindow->glfw) {
        glfwSetWindowShouldClose(g_currentWindow->glfw, GLFW_TRUE);
    }
}

BOOL PeekMessageA(MSG* msg, HWND hWnd, UINT, UINT, UINT removeMsg) {
    if (!hWnd || !msg) return FALSE;
    std::lock_guard<std::mutex> lk(hWnd->mu);
    if (hWnd->queue.empty()) return FALSE;
    *msg = hWnd->queue.front();
    if (removeMsg & PM_REMOVE) hWnd->queue.pop();
    return TRUE;
}

BOOL TranslateMessage(const MSG*) { return FALSE; }

LRESULT DispatchMessageA(const MSG* msg) {
    if (!msg || !msg->hwnd || !msg->hwnd->wndProc) return 0;
    return msg->hwnd->wndProc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

// ============================================================
// 注册表（返回真实主题信息）
// ============================================================
namespace {
    struct FakeRegKey { int tag; };
    FakeRegKey g_fakeKey{ 1 };

    bool QueryGnomeDarkMode() {
        FILE* f = ::popen(
            "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null",
            "r");
        if (!f) return false;
        char buf[128] = { 0 };
        if (!std::fgets(buf, sizeof(buf), f)) { ::pclose(f); return false; }
        ::pclose(f);
        std::string s(buf);
        return s.find("dark") != std::string::npos;
    }

    DWORD QueryGnomeAccentColor() {
        return 0xFF0078D4;
    }
}  // namespace

LONG RegOpenKeyExW(HKEY, LPCWSTR, DWORD, DWORD, HKEY* result) {
    if (result) *result = &g_fakeKey;
    return ERROR_SUCCESS;
}

LONG RegQueryValueExW(HKEY, LPCWSTR valueName, DWORD*, DWORD* type,
    BYTE* data, DWORD* size) {
    if (!valueName || !data || !size) return 1;

    std::string name = Utf32ToUtf8(valueName);

    if (name == "AppsUseLightTheme") {
        DWORD val = QueryGnomeDarkMode() ? 0 : 1;
        if (*size >= sizeof(DWORD)) {
            std::memcpy(data, &val, sizeof(DWORD));
            *size = sizeof(DWORD);
            if (type) *type = 4;
            return ERROR_SUCCESS;
        }
    }
    if (name == "AccentColorMenu") {
        DWORD val = QueryGnomeAccentColor();
        if (*size >= sizeof(DWORD)) {
            std::memcpy(data, &val, sizeof(DWORD));
            *size = sizeof(DWORD);
            if (type) *type = 4;
            return ERROR_SUCCESS;
        }
    }
    *size = 0;
    return 2;
}

LONG RegCloseKey(HKEY) { return ERROR_SUCCESS; }

// ============================================================
// 热键（X11）
// ============================================================
namespace {

#if HAS_X11
    unsigned int ModsToX11(UINT mods) {
        unsigned int x = 0;
        if (mods & MOD_CONTROL) x |= ControlMask;
        if (mods & MOD_ALT)     x |= Mod1Mask;
        if (mods & MOD_SHIFT)   x |= ShiftMask;
        if (mods & MOD_WIN)     x |= Mod4Mask;
        return x;
    }

    KeySym VkToKeySym(UINT vk) {
        switch (vk) {
        case VK_OEM_PERIOD: return XK_period;
        case VK_OEM_COMMA:  return XK_comma;
        case VK_OEM_2:      return XK_slash;
        case 0x4C:          return XK_l;
        case 0x58:          return XK_x;
        default:            return (KeySym)vk;
        }
    }
#endif

}  // namespace

BOOL RegisterHotKey(HWND hWnd, int id, UINT mods, UINT vk) {
    if (!hWnd) return FALSE;

#if HAS_X11
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return FALSE;

    unsigned int xmods = ModsToX11(mods);
    KeySym ks = VkToKeySym(vk);
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (!kc) { XCloseDisplay(dpy); return FALSE; }

    Window root = DefaultRootWindow(dpy);
    XGrabKey(dpy, kc, xmods, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, kc, xmods | Mod2Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, kc, xmods | LockMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, kc, xmods | Mod2Mask | LockMask, root, True,
        GrabModeAsync, GrabModeAsync);
    XSync(dpy, False);
    XCloseDisplay(dpy);

    hWnd->hotkeys.push_back({ id, mods, vk });
    return TRUE;
#else
    (void)id; (void)mods; (void)vk;
    OutputDebugStringA("RegisterHotKey: Wayland 不支持全局热键\n");
    return FALSE;
#endif
}

BOOL UnregisterHotKey(HWND hWnd, int) {
    if (!hWnd) return FALSE;
#if HAS_X11
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return FALSE;
    Window root = DefaultRootWindow(dpy);
    for (auto& e : hWnd->hotkeys) {
        unsigned int xmods = ModsToX11(e.mods);
        KeySym ks = VkToKeySym(e.vk);
        KeyCode kc = XKeysymToKeycode(dpy, ks);
        if (kc) {
            XUngrabKey(dpy, kc, xmods, root);
            XUngrabKey(dpy, kc, xmods | Mod2Mask, root);
            XUngrabKey(dpy, kc, xmods | LockMask, root);
            XUngrabKey(dpy, kc, xmods | Mod2Mask | LockMask, root);
        }
    }
    XSync(dpy, False);
    XCloseDisplay(dpy);
    hWnd->hotkeys.clear();
#endif
    return TRUE;
}

// 每帧检查 X11 事件队列里是否有热键按下
static void CheckX11Hotkeys(HWND hWnd) {
#if HAS_X11
    if (!hWnd || hWnd->hotkeys.empty()) return;
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            for (auto& e : hWnd->hotkeys) {
                if (ks == VkToKeySym(e.vk)) {
                    PostMessageA(hWnd, WM_HOTKEY, (WPARAM)e.id, 0);
                    break;
                }
            }
        }
    }
    XCloseDisplay(dpy);
#endif
}

// ============================================================
// 网络
// ============================================================
int WSAStartup(WORD, WSADATA* data) {
    if (data) { data->wVersion = 0x0202; data->wHighVersion = 0x0202; }
    return 0;
}

int WSACleanup() { return 0; }

int closesocket(SOCKET s) { return ::close(s); }

int InetPtonA(int family, LPCSTR src, PVOID dst) {
    return ::inet_pton(family, src, dst);
}

// ============================================================
// 其他
// ============================================================
HRESULT SetCurrentProcessExplicitAppUserModelID(LPCWSTR) {
    return S_OK;
}

// ============================================================
// 工厂函数 + 事件泵
// ============================================================
HWND platform_CreateGlfwWindow(int width, int height, const char* title) {
    if (!glfwInit()) {
        std::fprintf(stderr, "[GLFW] init failed\n");
        return nullptr;
    }

    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "[GLFW] create window failed\n");
        glfwTerminate();
        return nullptr;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    PlatformWindow* w = new PlatformWindow();
    w->glfw = win;
    w->lastLeftDown = false;
    w->lastRightDown = false;

    glfwSetWindowUserPointer(win, w);

    g_currentWindow = w;
    return w;
}

void platform_DestroyGlfwWindow(HWND w) {
    if (!w) return;
    if (w->glfw) {
        glfwDestroyWindow(w->glfw);
        w->glfw = nullptr;
    }
    if (g_currentWindow == w) g_currentWindow = nullptr;
    delete w;
}

bool platform_WindowShouldClose(HWND w) {
    if (!w || !w->glfw) return true;
    return glfwWindowShouldClose(w->glfw) != 0;
}

void platform_SwapBuffers(HWND w) {
    if (!w || !w->glfw) return;
    glfwSwapBuffers(w->glfw);
}

void platform_PumpEvents(HWND w) {
    if (!w) return;

    glfwPollEvents();
    PostMouseButtonEvents(w);
    CheckX11Hotkeys(w);
    ProcessTimers(w);

    MSG msg;
    while (PeekMessageA(&msg, w, 0, 0, PM_REMOVE)) {
        DispatchMessageA(&msg);
    }
}

// ============================================================
// platform_compat 命名空间
// ============================================================
namespace platform_compat {

    void SetCurrentWindow(PlatformWindow* w) { g_currentWindow = w; }
    PlatformWindow* GetCurrentWindow() { return g_currentWindow; }

    void SetWndProc(PlatformWindow* w, WndProcFn fn) {
        if (w) w->wndProc = fn;
    }

    void ProcessEvents() {
        if (g_currentWindow) platform_PumpEvents(g_currentWindow);
    }

}  // namespace platform_compat