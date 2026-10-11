#include <platform/platform.h>

#if PLATFORM_LINUX

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <GL/gl.h>
#include <chrono>

#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl2.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <unistd.h>    // readlink, execl
#include <climits>     // PATH_MAX

#include <sys/file.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

#include <X11/Xatom.h>

#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#include <GLFW/glfw3native.h>   // glfwGetX11Window / glfwGetWaylandWindow
#include <curl/curl.h>
#include <fstream>

#include <filesystem>  

// ═══════════════════════════════════════════════════════════
// 全局句柄
// ═══════════════════════════════════════════════════════════
WindowHandle g_hWnd = nullptr;
static GLFWwindow* g_glfwWindow = nullptr;
static Display* g_xDisplay = nullptr;
static Window   g_xRoot    = 0;
// ═══════════════════════════════════════════════════════════
// 消息队列
// ═══════════════════════════════════════════════════════════
namespace {
    struct MessageQueue {
        std::deque<MSG>         queue;
        mutable std::mutex      mtx;
        std::condition_variable cv;
        static MessageQueue& Instance() { static MessageQueue q; return q; }
        void Push(const MSG& m) {
            { std::lock_guard<std::mutex> lock(mtx); queue.push_back(m); }
            cv.notify_one();
        }
        bool TryPop(MSG& out) {
            std::lock_guard<std::mutex> lock(mtx);
            if (queue.empty()) return false;
            out = queue.front(); queue.pop_front(); return true;
        }
    };
}

BOOL PostMessage(HWND, UINT msg, WPARAM wParam, LPARAM lParam) {
    MSG m{}; m.message = msg; m.wParam = wParam; m.lParam = lParam;
    MessageQueue::Instance().Push(m);
    return TRUE;
}
void PostQuitMessage(int) { PostMessage(nullptr, WM_QUIT, 0, 0); }
BOOL PeekMessage(MSG* msg, HWND, UINT, UINT, UINT) {
    if (!msg) return FALSE;
    return MessageQueue::Instance().TryPop(*msg) ? TRUE : FALSE;
}
void TranslateMessage(MSG*) {}
void DispatchMessage(MSG*) {}

// ═══════════════════════════════════════════════════════════
// 调试输出
// ═══════════════════════════════════════════════════════════
void OutputDebugStringA(const char* s) { if (s) fputs(s, stderr); }

// ═══════════════════════════════════════════════════════════
// 窗口外观（全部空实现，暂时不影响运行）
// ═══════════════════════════════════════════════════════════
void EnableAcrylic(WindowHandle, uint8_t, uint32_t) {}
bool IsDarkModeByRegistry() { return false; }
uint32_t GetAccentColorFromRegistry() { return 0xFF0078D4; }

// ═══════════════════════════════════════════════════════════
// 窗口管理
// ═══════════════════════════════════════════════════════════
bool InitWindowPlat(const std::string& className) {
    (void)className;

    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);

    if (!glfwInit()) {
        OutputDebugStringA("[linux] glfwInit failed\n");
        return false;
    }

    // 树莓派：OpenGL 2.1
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    // 无边框 + 透明
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    g_glfwWindow = glfwCreateWindow(700, 230, "Vacuum Music Player", nullptr, nullptr);
    if (!g_glfwWindow) {
        const char* desc;
        glfwGetError(&desc);
        OutputDebugStringA(("[linux] glfwCreateWindow failed: " +
            std::string(desc ? desc : "unknown") + "\n").c_str());
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_glfwWindow);
    glfwSwapInterval(1);
    g_hWnd = (WindowHandle)g_glfwWindow;

    OutputDebugStringA("[linux] GLFW window created\n");
    return true;
}

// ═══════════════════════════════════════════════════════════
// 重启自己
//
// 用 execl 替换当前进程映像。cwd、环境变量都保留，
// 新进程从 main() 重新开始跑。
//
// 依赖：
//   - /proc/self/exe 拿到自己的绝对路径（不依赖 cwd）
//   - O_CLOEXEC 的锁 fd 会在 execl 时自动关闭 → 单实例锁释放
// ═══════════════════════════════════════════════════════════
void RestartApplication() {
    // 1. 刷所有缓冲，避免日志丢
    fflush(nullptr);

    // 2. 取自己的可执行文件路径
    char exePath[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) {
        fprintf(stderr, "[restart] readlink(/proc/self/exe) failed\n");
        exit(1);
    }
    exePath[len] = '\0';

    fprintf(stderr, "[restart] relaunching: %s\n", exePath);

    // 3. 替换进程
    //    argv[0] = exePath（约定俗成）
    //    环境变量、cwd 自动继承
    //    O_CLOEXEC 的 fd 自动关闭
    execl(exePath, exePath, (char*)nullptr);

    // 4. 只有 execl 失败才会执行到这里
    fprintf(stderr, "[restart] execl failed: %s\n", strerror(errno));
    exit(1);
}
void CleanupWindowPlat() {
    if (g_glfwWindow) {
        glfwDestroyWindow(g_glfwWindow);
        g_glfwWindow = nullptr;
    }
    glfwTerminate();
}
RECT GetWindowRectPlat(WindowHandle) {
    if (!g_glfwWindow) return {0, 0, 0, 0};
    int x = 0, y = 0, w = 0, h = 0;
    glfwGetWindowPos(g_glfwWindow, &x, &y);
    glfwGetWindowSize(g_glfwWindow, &w, &h);
    RECT rc;
    rc.left = x; rc.top = y;
    rc.right = x + w; rc.bottom = y + h;
    return rc;
}
RECT GetWorkAreaPlat() {
    // 用 GLFW 获取屏幕工作区（不含任务栏）
    if (!g_glfwWindow) return {0, 0, 1920, 1080};
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return {0, 0, 1920, 1080};
    int x = 0, y = 0, w = 0, h = 0;
    glfwGetMonitorWorkarea(monitor, &x, &y, &w, &h);
    RECT rc;
    rc.left = x; rc.top = y;
    rc.right = x + w; rc.bottom = y + h;
    return rc;
}
int GetScreenWidth()  { return 1920; }
int GetScreenHeight() { return 1080; }
void MoveWindowPlat(WindowHandle, int x, int y) {
    if (!g_glfwWindow) return;
    glfwSetWindowPos(g_glfwWindow, x, y);
}

void MoveResizeWindowPlat(WindowHandle, int x, int y, int w, int h) {
    if (!g_glfwWindow) return;

    // ★ X11 下宽高不能为 0，加兜底
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    glfwSetWindowSize(g_glfwWindow, w, h);
    glfwSetWindowPos(g_glfwWindow, x, y);
}
void SetWindowAlpha(WindowHandle, uint8_t alpha) {
    if (!g_glfwWindow) return;

    float opacity = alpha / 255.0f;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;

    // ── 方法 1：GLFW 跨平台 API ──
    // 内部会按当前后端走：Wayland 协议 / X11 属性 / Windows DWM / macOS
    glfwSetWindowOpacity(g_glfwWindow, opacity);

    // ── 方法 2：X11 原生属性（XWayland / 原生 X11 下更直接）──
    // Wayland 后端下 glfwGetX11Window 返回 nullptr，自动跳过
    if (g_xDisplay && glfwGetPlatform() == GLFW_PLATFORM_X11) {
        ::Window x11win = glfwGetX11Window(g_glfwWindow);
        if (x11win) {
            unsigned long op = (unsigned long)(opacity * 0xFFFFFFFFul);
            Atom atom = XInternAtom(g_xDisplay, "_NET_WM_WINDOW_OPACITY", False);
            XChangeProperty(g_xDisplay, x11win,
                            atom, XA_CARDINAL, 32,
                            PropModeReplace,
                            (unsigned char*)&op, 1);
            XFlush(g_xDisplay);
        }
    }
}
void CaptureMouse(WindowHandle) {}
void ReleaseMouseCapture() {}
POINT GetMousePosPlat() {
    // X11 可用时用 XQueryPointer（屏幕绝对坐标，不受窗口位置影响）
    if (g_xDisplay && g_xRoot) {
        ::Window root_ret, child_ret;
        int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
        unsigned int mask = 0;
        if (XQueryPointer(g_xDisplay, g_xRoot,
                          &root_ret, &child_ret,
                          &root_x, &root_y,
                          &win_x, &win_y, &mask)) {
            POINT pt;
            pt.x = root_x;
            pt.y = root_y;
            return pt;
        }
    }

    // 回退：GLFW 相对坐标 + 窗口位置
    POINT pt{0, 0};
    if (g_glfwWindow) {
        double x = 0, y = 0;
        glfwGetCursorPos(g_glfwWindow, &x, &y);
        int wx = 0, wy = 0;
        glfwGetWindowPos(g_glfwWindow, &wx, &wy);
        pt.x = wx + (long)x;
        pt.y = wy + (long)y;
    }
    return pt;
}

namespace {
    bool g_timerActive = false;
    int  g_timerIntervalMs = 10;
    std::chrono::steady_clock::time_point g_timerNextFire;
}

uintptr_t StartWindowTimer(WindowHandle, int intervalMs) {
    g_timerActive = true;
    g_timerIntervalMs = intervalMs;
    g_timerNextFire = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(intervalMs);
    return 12345;
}

void StopWindowTimer(WindowHandle, uintptr_t timerId) {
    if (timerId == 12345) g_timerActive = false;
}

void LnxProcessTimers() {
    if (!g_timerActive) return;
    auto now = std::chrono::steady_clock::now();
    if (now >= g_timerNextFire) {
        g_timerNextFire = now + std::chrono::milliseconds(g_timerIntervalMs);
        PostMessage(g_hWnd, WM_TIMER, 12345, 0);
    }
}

// ═══════════════════════════════════════════════════════════
// 纹理（OpenGL 版，先做基本的）
// ═══════════════════════════════════════════════════════════
TextureHandle CreateTextureFromRGBA(const std::vector<unsigned char>& rgbaData,int width, int height) {
    if (rgbaData.empty() || width <= 0 || height <= 0) return nullptr;
    if (rgbaData.size() != static_cast<size_t>(width) * height * 4) return nullptr;

    GLuint texId = 0;
    glGenTextures(1, &texId);
    if (texId == 0) return nullptr;

    glBindTexture(GL_TEXTURE_2D, texId);

    // ★ 关键：这两行必须有
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 对齐
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
    width, height, 0,
    GL_RGBA, GL_UNSIGNED_BYTE,
    rgbaData.data());

    glBindTexture(GL_TEXTURE_2D, 0);
    OutputDebugStringA(("[tex] id=" + std::to_string(texId) +" " + std::to_string(width) + "x" + std::to_string(height) + "\n").c_str());

    return reinterpret_cast<TextureHandle>(static_cast<intptr_t>(texId));
}
TextureHandle CreateTextureFromImageData(const std::vector<unsigned char>&) { return nullptr; }
void ReleaseTexture(TextureHandle tex) {
    if (!tex) return;
    GLuint id = static_cast<GLuint>(reinterpret_cast<intptr_t>(tex));
    glDeleteTextures(1, &id);
}

// ═══════════════════════════════════════════════════════════
// 窗口初始化（GLFW 之前先空着）
// ═══════════════════════════════════════════════════════════
bool CreateDeviceD3D(WindowHandle) { return true; }

// ═══════════════════════════════════════════════════════════
// SMTC（Windows 专属，Linux 空实现）
// ═══════════════════════════════════════════════════════════
void InitSMTC(WindowHandle) {}
void SetSMTCPlaybackStatus(PlaybackStatus) {}
void UpdateSMTC(const std::string&, const std::string&, const std::string&, const std::vector<unsigned char>&) {}

// ═══════════════════════════════════════════════════════════
// 任务栏
// ═══════════════════════════════════════════════════════════
bool InitTaskbarProgress(WindowHandle) { return false; }
void SetTaskbarProgress(uint64_t, uint64_t) {}
void SetTaskbarState(bool) {}
bool HandleTaskbarMessage(unsigned int) { return false; }

// ═══════════════════════════════════════════════════════════
// 热键
// ═══════════════════════════════════════════════════════════
void InitHotkey(WindowHandle) {
    g_xDisplay = XOpenDisplay(nullptr);
    if (!g_xDisplay) {
        OutputDebugStringA("[hotkey] XOpenDisplay failed (Wayland native?)\n");
        return;
    }
    g_xRoot = DefaultRootWindow(g_xDisplay);

    auto grab = [&](KeySym key) {
        KeyCode code = XKeysymToKeycode(g_xDisplay, key);
        if (code == 0) return;
        // 抓 4 种组合，处理 CapsLock / NumLock
        unsigned int extra[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
        for (auto e : extra) {
            XGrabKey(g_xDisplay, code, ControlMask | Mod1Mask | e, g_xRoot,
                     True, GrabModeAsync, GrabModeAsync);
        }
    };

    grab(XK_comma);    // Ctrl+Alt+,  上一首
    grab(XK_period);   // Ctrl+Alt+.  下一首
    grab(XK_slash);    // Ctrl+Alt+/  播放暂停
    grab(XK_h);        // Ctrl+Alt+H  显示/隐藏 (区别于windows,linux桌面会注销)
    grab(XK_x);        // Ctrl+Alt+X  退出

    XFlush(g_xDisplay);
    OutputDebugStringA("[hotkey] XGrabKey registered\n");
}
void UnregisterAllHotkeys() {
    if (!g_xDisplay) return;
    XUngrabKey(g_xDisplay, AnyKey, AnyModifier, g_xRoot);
    XCloseDisplay(g_xDisplay);
    g_xDisplay = nullptr;
}
void UnregisterSessionNotification() {}

void LnxPollX11Events() {
    if (!g_xDisplay) return;
    int n = XPending(g_xDisplay);
    if (n > 0) {
        OutputDebugStringA(("[x11] " + std::to_string(n) + " events pending\n").c_str());
    }
    while (XPending(g_xDisplay)) {
        XEvent ev;
        XNextEvent(g_xDisplay, &ev);
        OutputDebugStringA(("[x11] event type=" + std::to_string(ev.type) + "\n").c_str());
        if (ev.type != KeyPress) continue;

        KeyCode code = ev.xkey.keycode;
        unsigned int mods = ev.xkey.state & ~(LockMask | Mod2Mask);
        unsigned int ctrlAlt = ControlMask | Mod1Mask;
        if ((mods & ctrlAlt) != ctrlAlt) continue;

        if      (code == XKeysymToKeycode(g_xDisplay, XK_comma))
            PostMessage(g_hWnd, WM_HOTKEY, HOTKEY_PREV, 0);
        else if (code == XKeysymToKeycode(g_xDisplay, XK_period))
            PostMessage(g_hWnd, WM_HOTKEY, HOTKEY_NEXT, 0);
        else if (code == XKeysymToKeycode(g_xDisplay, XK_slash))
            PostMessage(g_hWnd, WM_HOTKEY, HOTKEY_PLAYPAUSE, 0);
        else if (code == XKeysymToKeycode(g_xDisplay, XK_h))
            PostMessage(g_hWnd, WM_HOTKEY, HOTKEY_SHOWHIDE, 0);
        else if (code == XKeysymToKeycode(g_xDisplay, XK_x))
            PostMessage(g_hWnd, WM_HOTKEY, HOTKEY_QUIT, 0);
    }
}


// ═══════════════════════════════════════════════════════════
// 路径
// ═══════════════════════════════════════════════════════════
PathType Utf8ToPath(const std::string& utf8) { return utf8; }
std::string PathToUtf8(const PathType& path) { return path; }

// ═══════════════════════════════════════════════════════════
// 文件
// ═══════════════════════════════════════════════════════════
std::string GetTempDir() {
    // 优先级：TMPDIR > XDG_RUNTIME_DIR > /tmp/
    // 注意：Linux 上有 $TMPDIR 才是"用户认为的临时目录"
    const char* env = std::getenv("TMPDIR");
    if (env && env[0]) {
        std::string dir = env;
        if (dir.back() != '/') dir += '/';
        return dir;
    }

    env = std::getenv("XDG_RUNTIME_DIR");
    if (env && env[0]) {
        std::string dir = env;
        if (dir.back() != '/') dir += '/';
        return dir;
    }

    return "/tmp/";
}

std::string ReadTextFile(const PathType& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf(len, '\0');
    if (len > 0) fread(buf.data(), 1, len, f);
    fclose(f);
    return buf;
}

namespace {

    // 写入回调：把收到的字节写进 FILE*
    size_t CurlWriteToFile(void* ptr, size_t size, size_t nmemb, void* userdata) {
        FILE* fp = static_cast<FILE*>(userdata);
        return std::fwrite(ptr, size, nmemb, fp);
    }

    // 进度回调：以后要取消下载可以接个 atomic<bool>
    // 现在先返回 0（继续），保留钩子方便扩展
    int CurlProgressCallback(void* /*clientp*/,
                             curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
        return 0;   // 返回非 0 会中止传输
    }

} // namespace

bool DownloadFile(const std::string& url, const std::string& localPath) {
    if (url.empty() || localPath.empty()) {
        OutputDebugStringA("[download] empty url or path\n");
        return false;
    }

    // 1. 保证父目录存在
    {
        std::filesystem::path p(localPath);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
            if (ec) {
                OutputDebugStringA(("[download] mkdir failed: " + ec.message() + "\n").c_str());
                return false;
            }
        }
    }

    // 2. 初始化 easy handle
    CURL* curl = curl_easy_init();
    if (!curl) {
        OutputDebugStringA("[download] curl_easy_init failed\n");
        return false;
    }

    // 3. 打开输出文件
    FILE* fp = std::fopen(localPath.c_str(), "wb");
    if (!fp) {
        OutputDebugStringA(("[download] fopen failed: " + localPath + "\n").c_str());
        curl_easy_cleanup(curl);
        return false;
    }

    // 4. 加 UA（网易云 CDN 有时会校验 UA，不带会被拦）
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers,
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0 Safari/537.36");

    // 5. 配置
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);   // 跟随 3xx
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 连接超时 10s
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);        // 总超时 120s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // 低速阈值
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);  // 30s 内 <1B/s 则断
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);         // 多线程环境必须
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // 自动 gzip/deflate
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 6. 执行
    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    // 7. 清理 curl 资源
    std::fclose(fp);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 8. 判定结果
    auto removeBrokenFile = [&]() {
        std::error_code ec;
        std::filesystem::remove(localPath, ec);
    };

    if (res != CURLE_OK) {
        OutputDebugStringA(("[download] curl failed: " +
            std::string(curl_easy_strerror(res)) + " url=" + url + "\n").c_str());
        removeBrokenFile();
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        OutputDebugStringA(("[download] HTTP " + std::to_string(httpCode) +
            " url=" + url + "\n").c_str());
        removeBrokenFile();
        return false;
    }

    // 文件大小 > 0 才算成功
    std::error_code ec;
    auto sz = std::filesystem::file_size(localPath, ec);
    if (ec || sz == 0) {
        OutputDebugStringA("[download] empty file after transfer\n");
        removeBrokenFile();
        return false;
    }

    OutputDebugStringA(("[download] OK " + std::to_string(sz) + " bytes -> " +
        localPath + "\n").c_str());
    return true;
}

// ═══════════════════════════════════════════════════════════
// 消息框（zenity）
//
// zenity 是 GNOME 系自带的对话框工具，KDE 上有 kdialog 等价物。
// 用 system() 调用，不引入 GTK 链接依赖。
//
// 注意：
//   - 需要 $DISPLAY（X11/XWayland）
//   - zenity 没装时回退到 stderr 输出
// ═══════════════════════════════════════════════════════════
namespace {

    // 单引号包裹，转义内部单引号 —— 防 shell 注入
    std::string ShellEscape(const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else           out += c;
        }
        out += "'";
        return out;
    }

    // 检测 zenity 是否可用（首次调用缓存结果）
    bool HasZenity() {
        static int cached = -1;
        if (cached < 0) {
            cached = (system("command -v zenity >/dev/null 2>&1") == 0) ? 1 : 0;
        }
        return cached == 1;
    }

} // namespace

void ShowMessageBox(const std::string& title, const std::string& msg) {
    if (HasZenity()) {
        std::string cmd = "zenity --info"
                          " --title=" + ShellEscape(title) +
                          " --text="  + ShellEscape(msg) +
                          " --width=400"
                          " 2>/dev/null";
        int ret = system(cmd.c_str());
        if (ret == 0) return;   // 弹窗成功
    }
    // 回退：stderr 输出
    fprintf(stderr, "\n[%s] %s\n", title.c_str(), msg.c_str());
}

bool ShowConfirmBox(const std::string& title, const std::string& msg) {
    if (HasZenity()) {
        std::string cmd = "zenity --question"
                          " --title=" + ShellEscape(title) +
                          " --text="  + ShellEscape(msg) +
                          " --width=400"
                          " 2>/dev/null";
        int ret = system(cmd.c_str());
        // zenity --question: 点"是"退出码 0，点"否"退出码 1
        return ret == 0;
    }
    // 回退：默认否
    fprintf(stderr, "\n[%s] %s (auto: no)\n", title.c_str(), msg.c_str());
    return false;
}
std::string PickFolderDialog(WindowHandle) {
    char buf[512]{};
    FILE* pipe = popen("zenity --file-selection --directory 2>/dev/null", "r");
    if (!pipe) return "";
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(pipe);
    return result;
}

// ═══════════════════════════════════════════════════════════
// 加解密（Linux 明文返回，以后可换 OpenSSL）
// ═══════════════════════════════════════════════════════════
bool EncryptString(const std::string& in, std::string& out) { out = in; return true; }
bool DecryptString(const std::string& in, std::string& out) { out = in; return true; }

// ═══════════════════════════════════════════════════════════
// 其他
// ═══════════════════════════════════════════════════════════
void OpenUrlInBrowser(const std::string& url) {
    std::string cmd = "xdg-open \"" + url + "\" &";
    system(cmd.c_str());
}

std::vector<unsigned char> DecodeAndScaleImage(
    const std::vector<unsigned char>& imageData,
    int targetWidth, int targetHeight)
{
    if (imageData.empty() || targetWidth <= 0 || targetHeight <= 0)
        return {};

    // 1. 解码（内存 → RGBA）
    int srcW = 0, srcH = 0, srcChannels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        imageData.data(),       // 输入：编码数据指针
        (int)imageData.size(),  // 输入：数据长度
        &srcW, &srcH,           // 输出：宽、高
        &srcChannels,           // 输出：原图通道数（3=RGB, 4=RGBA）
        4                       // 强制要求输出 4 通道（RGBA）
    );
    if (!pixels) {
        OutputDebugStringA(("[stbi] FAIL: " + std::string(stbi_failure_reason()) + "\n").c_str());
        return {};
    }
    OutputDebugStringA(("[stbi] decode " + std::to_string(srcW) + "x" + std::to_string(srcH) +
                        ", channels=" + std::to_string(srcChannels) + "\n").c_str());

    // 2. 缩放（stb_image_resize2）
    std::vector<unsigned char> out(targetWidth * targetHeight * 4);
    stbir_resize_uint8_srgb(
        pixels, srcW, srcH, 0,
        out.data(), targetWidth, targetHeight, 0,
        STBIR_RGBA);

    stbi_image_free(pixels);
    return out;
}
std::vector<unsigned char> ConvertImageToJPEG(const std::vector<unsigned char>&) { return {}; }

std::string GetFileVersionString() { return "1.0.0"; }
TextureHandle LoadTextureFromResource(int, const wchar_t*, int*, int*) { return nullptr; }
TextureHandle LoadAppIcon(int*, int*) { return nullptr; }


void RequestQuit() { PostMessage(g_hWnd, WM_CLOSE, 0, 0); }
void RequestQuitProgram() { PostQuitMessage(0); }

bool InitNetwork() {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        OutputDebugStringA(("[net] curl_global_init failed: " +
            std::string(curl_easy_strerror(res)) + "\n").c_str());
        return false;
    }
    return true;
}

void CleanupNetwork() {
    curl_global_cleanup();
}

int FindAvailablePort(int minPort, int) { return minPort; }

// ═══════════════════════════════════════════════════════════
// 网卡枚举（Linux）
//
// Windows 用 GetAdaptersInfo，Linux 用 getifaddrs +
//   /proc/net/route（判断默认路由）
//   /sys/class/net/<iface>/device/uevent（读 driver 名）
//
// description 拼成 "iface (driver)"，让上层的过滤/打分逻辑
// 在 Linux 上也能工作。
// ═══════════════════════════════════════════════════════════

namespace {
    // 读网卡驱动名（PCI 网卡才有）
    std::string GetIfaceDriver(const std::string& iface) {
        std::ifstream f("/sys/class/net/" + iface + "/device/uevent");
        if (!f) return "";
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("DRIVER=", 0) == 0)
                return line.substr(7);
        }
        return "";
    }

    // 读默认路由的网卡名（hasGateway 判定）
    std::string GetDefaultRouteIface() {
        std::ifstream f("/proc/net/route");
        if (!f) return "";
        std::string line;
        std::getline(f, line);  // skip header
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string iface, dest;
            iss >> iface >> dest;
            if (dest == "00000000") return iface;
        }
        return "";
    }
}

std::vector<NetAdapterInfo> EnumNetworkAdapters() {
    std::vector<NetAdapterInfo> result;

    std::string defaultIface = GetDefaultRouteIface();
    OutputDebugStringA(("[net] default route iface: " + 
                        (defaultIface.empty() ? "(none)" : defaultIface) + "\n").c_str());

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0 || !ifaddr) {
        OutputDebugStringA("[net] getifaddrs failed\n");
        return result;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;   // 只看 IPv4
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        // IP
        char ipBuf[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, ipBuf, sizeof(ipBuf));

        std::string ip = ipBuf;
        if (ip.empty() || ip == "0.0.0.0") continue;

        std::string ifaceName = ifa->ifa_name;

        // description = "iface (driver)"
        std::string driver = GetIfaceDriver(ifaceName);
        std::string description = ifaceName;
        if (!driver.empty()) description += " (" + driver + ")";

        NetAdapterInfo info;
        info.ip = ip;
        info.description = description;
        info.hasGateway = (ifaceName == defaultIface);

        // 类型：靠名字猜（Linux 网卡命名规范）
        if (ifaceName.rfind("wl", 0) == 0 || ifaceName.rfind("wlan", 0) == 0) {
            info.type = 2;   // WiFi
        } else if (ifaceName.rfind("eth", 0) == 0 ||
                   ifaceName.rfind("en", 0) == 0 ||
                   ifaceName.rfind("usb", 0) == 0) {
            info.type = 1;   // Ethernet
        } else {
            info.type = 0;   // unknown（veth、docker0、br-*、tun 等）
        }

        result.push_back(std::move(info));
        OutputDebugStringA(("[net] adapter: " + info.ip +
                            " | " + info.description +
                            " | hasGateway=" + (info.hasGateway ? "1" : "0") +
                            " | type=" + std::to_string(info.type) + "\n").c_str());
    }

    freeifaddrs(ifaddr);
    return result;
}

extern bool LnxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void LnxPollMouse() {
    if (!g_glfwWindow) return;

    static bool prevRightDown = false;
    static bool prevLeftDown  = false;
    static double prevX = 0, prevY = 0;
    static bool  firstFrame = true;

    // 鼠标按键
    bool rightDown = glfwGetMouseButton(g_glfwWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool leftDown  = glfwGetMouseButton(g_glfwWindow, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;

    if (rightDown && !prevRightDown)
        LnxProc(g_hWnd, WM_RBUTTONDOWN, 0, 0);
    else if (!rightDown && prevRightDown)
        LnxProc(g_hWnd, WM_RBUTTONUP, 0, 0);

    if (leftDown && !prevLeftDown) { /* 需要就补 WM_LBUTTONDOWN */ }
    else if (!leftDown && prevLeftDown)
        LnxProc(g_hWnd, WM_LBUTTONUP, 0, 0);

    prevRightDown = rightDown;
    prevLeftDown  = leftDown;

    // 鼠标移动
    double x = 0, y = 0;
    glfwGetCursorPos(g_glfwWindow, &x, &y);
    if (firstFrame) { prevX = x; prevY = y; firstFrame = false; }
    if (x != prevX || y != prevY) {
        LnxProc(g_hWnd, WM_MOUSEMOVE, 0, 0);
        prevX = x; prevY = y;
    }
}

void PrepareImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // 中文字体（找一个存在的）
    const char* fontPath = nullptr;
    const char* candidates[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",  // 不支持中文
    };
    for (auto p : candidates) {
        FILE* f = fopen(p, "rb");
        if (f) { fclose(f); fontPath = p; break; }
    }

    if (fontPath) {
        g_FontNormal = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseFull());
        g_FontLarge  = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseFull());
        if (g_FontNormal) io.FontDefault = g_FontNormal;
    }
    glfwSetWindowCloseCallback(g_glfwWindow, [](GLFWwindow* w) {
        glfwSetWindowShouldClose(w, GLFW_FALSE);
        PostMessage(g_hWnd, WM_CLOSE, 0, 0);
    });
    
    UpdateImGuiStyle();

    ImGui_ImplGlfw_InitForOpenGL(g_glfwWindow, true);
    ImGui_ImplOpenGL2_Init();
    OutputDebugStringA("[linux] ImGui initialized\n");
}

// ═══════════════════════════════════════════════════════════
// GLFW 主循环接口
// ═══════════════════════════════════════════════════════════
void LnxPollEvents()            { glfwPollEvents(); }
bool LnxWindowShouldClose()     { return g_glfwWindow && glfwWindowShouldClose(g_glfwWindow); }
void* LnxGetWindow()            { return g_glfwWindow; }

void LnxBeginFrame() {
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    // 注意：ImGui::NewFrame() 在 RenderFrame 里调
}

void LnxEndFrame() {
    if (!g_glfwWindow) return;

    // ★ 窗口隐藏时不 swap（避免阻塞）
    // 注意：g_isWindowHidden 在 main.cpp 里，这里访问不到。
    // 所以保护放在调用方（main 主循环）更合适。

    int w, h;
    glfwGetFramebufferSize(g_glfwWindow, &w, &h);
    if (w <= 0 || h <= 0) return;   // 无 framebuffer 时不画

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_glfwWindow);
}

static int g_lockFd = -1;

bool AcquireSingleInstanceLock() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    std::string lockPath = rt && rt[0]
        ? (std::string(rt) + "/vacuum_music_player.lock")
        : "/tmp/vacuum_music_player.lock";

    g_lockFd = open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (g_lockFd < 0) return true;   // 拿不到文件不阻止启动
    if (flock(g_lockFd, LOCK_EX | LOCK_NB) < 0) {
        close(g_lockFd);
        g_lockFd = -1;
        return false;
    }
    return true;
}

void ReleaseSingleInstanceLock() {
    if (g_lockFd >= 0) {
        flock(g_lockFd, LOCK_UN);
        close(g_lockFd);
        g_lockFd = -1;
    }
}

void HideWindowPlat(WindowHandle) {  //不建议使用快捷键隐藏 因为linux这边真的难做，快捷键在窗口隐藏下不好用 还是建议使用最小化功能（点击任务栏）
    if (!g_glfwWindow) return;
    glfwIconifyWindow(g_glfwWindow);
    OutputDebugStringA("[hide] iconified\n");
}

void ShowWindowPlat(WindowHandle) {
    if (!g_glfwWindow) return;
    // 最小化状态必须先 restore，否则 show/focus 都不生效
    if (glfwGetWindowAttrib(g_glfwWindow, GLFW_ICONIFIED)) {
        glfwRestoreWindow(g_glfwWindow);
    }
    if (!glfwGetWindowAttrib(g_glfwWindow, GLFW_VISIBLE)) {
        glfwShowWindow(g_glfwWindow);
    }
    glfwFocusWindow(g_glfwWindow);
}

#endif // PLATFORM_LINUX