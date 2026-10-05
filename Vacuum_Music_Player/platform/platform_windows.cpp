#include "platform.h"

#if PLATFORM_WINDOWS

// ─── 平台内部专属 include（main.cpp 看不到）───
#include <dwmapi.h>
#include <fstream>

#pragma comment(lib, "dwmapi.lib")

#include <random>
#include <thread>
#include <chrono>
#include <shellapi.h>

// ═══════════════════════════════════════════════════════════
// 全局句柄定义
// ═══════════════════════════════════════════════════════════
WindowHandle g_hWnd = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

ITaskbarList3* g_pTaskbarList = nullptr;

static std::wstring g_className;

HANDLE g_hMutex = NULL;

// 平台内部状态（main.cpp 无需访问）
static winrt::Windows::Media::Playback::MediaPlayer        g_mediaPlayer{ nullptr };
static winrt::Windows::Media::SystemMediaTransportControls g_smtc{ nullptr };
static UINT           g_uTaskbarBtnMsg = 0;

// ═══════════════════════════════════════════════════════════
// 1. 窗口外观
// ═══════════════════════════════════════════════════════════

// ─── 亚克力（DWM 未公开 API）───
typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE nAccentState;
    DWORD        nFlags;
    DWORD        nColor;      // AABBGGRR
    DWORD        nAnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    DWORD  Attrib;
    PVOID  pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

void EnableAcrylic(WindowHandle hWnd, uint8_t opacity, uint32_t color) {
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    if (!hUser32) return;

    auto pSetWindowCompositionAttribute =
        (BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*))
        GetProcAddress(hUser32, "SetWindowCompositionAttribute");
    if (!pSetWindowCompositionAttribute) return;

    ACCENT_POLICY policy{};
    policy.nAccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.nColor = (opacity << 24) | (color & 0xFFFFFF);

    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = 19;   // WCA_ACCENT_POLICY
    data.pvData = &policy;
    data.cbData = sizeof(policy);

    pSetWindowCompositionAttribute(hWnd, &data);
}

// 通过注册表检测深色/浅色模式
bool IsDarkModeByRegistry() {
    HKEY hKey;
    DWORD value = 1;      // 默认浅色
    DWORD size = sizeof(value);
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey);

    if (result == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }
    return (value == 0);   // 0 = 深色模式
}

uint32_t GetAccentColorFromRegistry()
{
    HKEY  hKey;
    DWORD accentColor = 0;              // ← 保留 DWORD：RegQueryValueExW 需要
    DWORD dataSize = sizeof(DWORD);  // ← 保留 DWORD：API 参数要求

    if (RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
        0,
        KEY_READ,
        &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(
            hKey,
            L"AccentColorMenu",
            NULL, NULL,
            (LPBYTE)&accentColor,
            &dataSize);

        RegCloseKey(hKey);
    }

    BYTE r = accentColor & 0xFF;
    BYTE g = (accentColor >> 8) & 0xFF;
    BYTE b = (accentColor >> 16) & 0xFF;

    // 组合成 ARGB (0xFFRRGGBB) 并作为 uint32_t 返回
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}


// ═══════════════════════════════════════════════════════════
// 2. D3D11
// ═══════════════════════════════════════════════════════════

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        featureLevels, 1, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr)) return false;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return SUCCEEDED(hr);
}

// ─── WIC 解码 → D3D11 纹理 ───
TextureHandle CreateTextureFromImageData(const std::vector<unsigned char>& imageData) {
    if (imageData.empty() || !g_pd3dDevice) return nullptr;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wicFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        CoUninitialize();
        return nullptr;
    }

    // 创建内存流
    IWICStream* stream = nullptr;
    hr = wicFactory->CreateStream(&stream);
    if (FAILED(hr)) {
        wicFactory->Release();
        CoUninitialize();
        return nullptr;
    }
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(imageData.data()), (DWORD)imageData.size());
    if (FAILED(hr)) {
        stream->Release();
        wicFactory->Release();
        CoUninitialize();
        return nullptr;
    }

    // 创建解码器
    IWICBitmapDecoder* decoder = nullptr;
    hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        stream->Release();
        wicFactory->Release();
        CoUninitialize();
        return nullptr;
    }

    // 获取第一帧
    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        decoder->Release();
        stream->Release();
        wicFactory->Release();
        CoUninitialize();
        return nullptr;
    }

    // 转换为 RGBA 格式
    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(hr)) {
            UINT width, height;
            frame->GetSize(&width, &height);

            std::vector<BYTE> pixelData(width * height * 4);
            hr = converter->CopyPixels(nullptr, width * 4, (UINT)pixelData.size(), pixelData.data());
            if (SUCCEEDED(hr)) {
                // 创建 D3D11 纹理
                D3D11_TEXTURE2D_DESC texDesc = {};
                texDesc.Width = width;
                texDesc.Height = height;
                texDesc.MipLevels = 1;
                texDesc.ArraySize = 1;
                texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                texDesc.SampleDesc.Count = 1;
                texDesc.Usage = D3D11_USAGE_DEFAULT;
                texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                D3D11_SUBRESOURCE_DATA initData = {};
                initData.pSysMem = pixelData.data();
                initData.SysMemPitch = width * 4;

                ID3D11Texture2D* texture = nullptr;
                hr = g_pd3dDevice->CreateTexture2D(&texDesc, &initData, &texture);  // ← 用 g_pd3dDevice
                if (SUCCEEDED(hr)) {
                    ID3D11ShaderResourceView* srv = nullptr;
                    g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &srv);  // ← 用 g_pd3dDevice
                    texture->Release();
                    converter->Release();
                    frame->Release();
                    decoder->Release();
                    stream->Release();
                    wicFactory->Release();
                    CoUninitialize();
                    return srv;
                }
            }
        }
        converter->Release();
    }
    frame->Release();
    decoder->Release();
    stream->Release();
    wicFactory->Release();
    CoUninitialize();
    return nullptr;
}

TextureHandle CreateTextureFromRGBA(const std::vector<unsigned char>& rgbaData, int width, int height) {
    if (rgbaData.empty() || !g_pd3dDevice) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;
    if (rgbaData.size() != static_cast<size_t>(width * height * 4)) return nullptr;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgbaData.data();
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = g_pd3dDevice->CreateTexture2D(&texDesc, &initData, &texture);  // ← 用 g_pd3dDevice
    if (FAILED(hr)) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &srv);  // ← 用 g_pd3dDevice
    texture->Release();
    return srv;
}
// ═══════════════════════════════════════════════════════════
// 3. SMTC
// ═══════════════════════════════════════════════════════════

void InitSMTC(WindowHandle hWnd) {
    g_mediaPlayer = winrt::Windows::Media::Playback::MediaPlayer();

    // 获取 SMTC 对象，这是核心步骤
    g_smtc = g_mediaPlayer.SystemMediaTransportControls();

    // 禁用 MediaPlayer 自带的自动集成，因为我们想手动控制
    g_mediaPlayer.CommandManager().IsEnabled(false);

    // 开启 SMTC
    g_smtc.IsEnabled(true);

    // 开启我们希望系统显示的控件按钮
    g_smtc.IsPlayEnabled(true);
    g_smtc.IsPauseEnabled(true);
    g_smtc.IsNextEnabled(true);
    g_smtc.IsPreviousEnabled(true);

    // 注册按钮点击事件（使用 lambda）
    g_smtc.ButtonPressed([](const auto& sender, const auto& args) {
        auto button = args.Button();
        switch (button) {
        case winrt::Windows::Media::SystemMediaTransportControlsButton::Play:
            PostMessage(g_hWnd, WM_APP_SMTC_PLAY, 0, 0);
            break;
        case winrt::Windows::Media::SystemMediaTransportControlsButton::Pause:
            PostMessage(g_hWnd, WM_APP_SMTC_PAUSE, 0, 0);
            break;
        case winrt::Windows::Media::SystemMediaTransportControlsButton::Next:
            // 下一首
            PostMessage(g_hWnd, WM_APP_SMTC_NEXT, 0, 0);
            break;
        case winrt::Windows::Media::SystemMediaTransportControlsButton::Previous:
            // 上一首
            PostMessage(g_hWnd, WM_APP_SMTC_PREV, 0, 0);
            break;
        default:
            break;
        }
        });

    SetSMTCPlaybackStatus(PlaybackStatus::Playing);
}

void SetSMTCPlaybackStatus(PlaybackStatus status) {
    using winrt::Windows::Media::MediaPlaybackStatus;
    MediaPlaybackStatus s = MediaPlaybackStatus::Closed;
    switch (status) {
    case PlaybackStatus::Playing: s = MediaPlaybackStatus::Playing; break;
    case PlaybackStatus::Paused:  s = MediaPlaybackStatus::Paused;  break;
    case PlaybackStatus::Stopped: s = MediaPlaybackStatus::Stopped; break;
    }
    g_smtc.PlaybackStatus(s);
}
void UpdateSMTC(
    const std::string& title,
    const std::string& artist,
    const std::string& album,
    const std::vector<unsigned char>& thumbnailData)
{
    using namespace winrt;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Media;
    using namespace Windows::Foundation;

    auto updater = g_smtc.DisplayUpdater();
    updater.ClearAll();
    updater.Type(MediaPlaybackType::Music);

    auto props = updater.MusicProperties();
    props.Title(to_hstring(title));
    props.Artist(to_hstring(artist));
    props.AlbumTitle(to_hstring(album));

    if (!thumbnailData.empty())
    {
        try
        {
            // ✅ 1. 每次创建新流（不要 static）
            InMemoryRandomAccessStream stream = InMemoryRandomAccessStream();

            // ✅ 2. 写入数据
            DataWriter writer(stream);
            writer.WriteBytes(array_view<const uint8_t>(thumbnailData.data(), thumbnailData.size()));

            // ✅ 3. 同步等待 StoreAsync 完成（不用 co_await）
            writer.StoreAsync().get();

            // ✅ 4. 关键：分离 Writer，让流独立存活
            writer.DetachStream();

            // ✅ 5. 确保流指针在最开始
            stream.Seek(0);

            // ✅ 6. 创建引用并设置
            auto streamRef = RandomAccessStreamReference::CreateFromStream(stream);
            updater.Thumbnail(streamRef);
        }
        catch (const hresult_error& e)
        {
            OutputDebugStringA(("SMTC Thumbnail Error: " + std::to_string(e.code()) + "\n").c_str());
        }
    }

    updater.Update();
}

// ═══════════════════════════════════════════════════════════
// 4. 密钥存储（DPAPI）
// ═══════════════════════════════════════════════════════════
#pragma comment(lib, "crypt32.lib")
#include <wincrypt.h>


// ═══════════════════════════════════════════════════════════
// 5. 任务栏进度
// ═══════════════════════════════════════════════════════════
#include <ShObjIdl.h>

bool InitTaskbarProgress(HWND hWnd) {
    // 注意：COM 已经被 winrt::init_apartment() 初始化，不要再调用 CoInitializeEx

    // 创建 ITaskbarList3 实例
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
        IID_ITaskbarList3, (void**)&g_pTaskbarList);
    if (FAILED(hr) || !g_pTaskbarList) {
        OutputDebugStringA("创建 ITaskbarList3 失败\n");
        return false;
    }

    // 注册 TaskbarButtonCreated 消息
    g_uTaskbarBtnMsg = RegisterWindowMessage(L"TaskbarButtonCreated");
    return true;
}

void SetTaskbarProgress(uint64_t current, uint64_t total) {
    if (!g_pTaskbarList || !g_hWnd) return;
    if (current == 0 || total == 0) return;
    g_pTaskbarList->SetProgressValue(g_hWnd, current, total);
}

void SetTaskbarState(bool playing) {
    if (!g_pTaskbarList || !g_hWnd) return;
    g_pTaskbarList->SetProgressState(g_hWnd, playing ? TBPF_NORMAL : TBPF_PAUSED);
}

bool HandleTaskbarMessage(unsigned int msg) {
    if (g_uTaskbarBtnMsg != 0 && msg == g_uTaskbarBtnMsg) {
        if (g_pTaskbarList) g_pTaskbarList->HrInit();
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════
// 6. 热键
// ═══════════════════════════════════════════════════════════
void InitHotkey(WindowHandle hWnd) {
    if (!RegisterHotKey(hWnd, HOTKEY_NEXT, MOD_CONTROL | MOD_ALT, VK_OEM_PERIOD))
        OutputDebugStringA("注册下一首热键失败\n");
    if (!RegisterHotKey(hWnd, HOTKEY_PREV, MOD_CONTROL | MOD_ALT, VK_OEM_COMMA))
        OutputDebugStringA("注册上一首热键失败\n");
    if (!RegisterHotKey(hWnd, HOTKEY_PLAYPAUSE, MOD_CONTROL | MOD_ALT, VK_OEM_2))
        OutputDebugStringA("注册播放/暂停热键失败\n");
    if (!RegisterHotKey(hWnd, HOTKEY_SHOWHIDE, MOD_CONTROL | MOD_ALT, 0x4C))
        OutputDebugStringA("注册隐藏/显示热键失败\n");
    if (!RegisterHotKey(hWnd, HOTKEY_QUIT, MOD_CONTROL | MOD_ALT, 0x58))
        OutputDebugStringA("注册退出热键失败\n");
}
void RequestQuit() { PostMessage(g_hWnd, WM_CLOSE, 0, 0); }

PathType Utf8ToPath(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len == 0) return {};
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], len);
    w.pop_back();  // 去掉末尾的 L'\0'
    return w;
}

int GetScreenWidth() { return GetSystemMetrics(SM_CXSCREEN); }
int GetScreenHeight() { return GetSystemMetrics(SM_CYSCREEN); }

RECT GetWindowRectPlat(WindowHandle hWnd) {
    RECT rc{};
    GetWindowRect(hWnd, &rc);
    return rc;
}

uintptr_t StartWindowTimer(WindowHandle hWnd, int intervalMs) {
    return SetTimer(hWnd, 12345, (UINT)intervalMs, nullptr);
}

void StopWindowTimer(WindowHandle hWnd, uintptr_t timerId) {
    if (timerId) KillTimer(hWnd, (UINT_PTR)timerId);
}

std::string GetTempDir() {
    char tempPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath) == 0) return {};
    return std::string(tempPath);
}

bool DownloadFile(const std::string& url, const std::string& localPath) {
    HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), localPath.c_str(), 0, NULL);
    return SUCCEEDED(hr);
}

std::string ReadTextFile(const PathType& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return {};

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);

    std::string buf(len, '\0');
    if (len > 0) fread(buf.data(), 1, len, file);
    fclose(file);

    return buf;
}

void SetWindowAlpha(WindowHandle hWnd, uint8_t alpha) {
    SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
}

void MoveWindowPlat(WindowHandle hWnd, int x, int y) {
    SetWindowPos(hWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void MoveResizeWindowPlat(WindowHandle hWnd, int x, int y, int w, int h) {
    SetWindowPos(hWnd, HWND_TOPMOST, x, y, w, h, SWP_NOZORDER | SWP_NOSENDCHANGING);
}

int FindAvailablePort(int minPort, int maxPort) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(minPort, maxPort);

    SOCKET testSock = INVALID_SOCKET;
    for (int attempt = 0; attempt < 1000; ++attempt) {   // 最多尝试 1000 次
        int port = dist(gen);
        testSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (testSock == INVALID_SOCKET) continue;
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);
        if (bind(testSock, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
            closesocket(testSock);
            return port;   // 绑定成功，说明端口空闲
        }
        closesocket(testSock);
    }
    return -1;   // 未找到可用端口
}
void ReleaseTexture(TextureHandle tex) {
    if (tex) tex->Release();
}

void UnregisterSessionNotification() {
    WTSUnRegisterSessionNotification(g_hWnd);
}

void UnregisterAllHotkeys() {
    UnregisterHotKey(g_hWnd, HOTKEY_NEXT);
    UnregisterHotKey(g_hWnd, HOTKEY_PREV);
    UnregisterHotKey(g_hWnd, HOTKEY_PLAYPAUSE);
    UnregisterHotKey(g_hWnd, HOTKEY_SHOWHIDE);
    UnregisterHotKey(g_hWnd, HOTKEY_QUIT);
}
bool InitNetwork() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}
void CleanupNetwork() { WSACleanup(); }

void RequestQuitProgram(){ PostQuitMessage(0); }

void ShowMessageBox(const std::string& title, const std::string& message) {
    PathType wTitle = Utf8ToPath(title);
    PathType wMsg = Utf8ToPath(message);
    MessageBoxW(g_hWnd, wMsg.c_str(), wTitle.c_str(), MB_OK | MB_ICONINFORMATION);
}

bool ShowConfirmBox(const std::string& title, const std::string& message) {
    PathType wTitle = Utf8ToPath(title);
    PathType wMsg = Utf8ToPath(message);
    return MessageBoxW(g_hWnd, wMsg.c_str(), wTitle.c_str(), MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
}

void CaptureMouse(WindowHandle hWnd) {
    SetCapture(hWnd);
}

void ReleaseMouseCapture() {
    ReleaseCapture();
}

POINT GetMousePosPlat() {
    POINT pt{};
    GetCursorPos(&pt);
    return pt;
}
bool EncryptString(const std::string& plaintext, std::string& ciphertext) {
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"AppCookie", nullptr, nullptr, nullptr, 0, &output))
        return false;

    ciphertext.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool DecryptString(const std::string& ciphertext, std::string& plaintext) {
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(ciphertext.data()));
    input.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
        return false;

    plaintext.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool InitWindowPlat(const std::string& className) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    PathType wClassName = Utf8ToPath(className);

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = wClassName.c_str();
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    g_hWnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_APPWINDOW | WS_EX_TOPMOST,
        wClassName.c_str(),
        L"Vacuum Music Player",
        WS_POPUP | WS_VISIBLE,
        0, 0, 700, 230,
        NULL, NULL, hInstance, NULL);

    if (!g_hWnd) return false;

    // WTS 会话通知（也是纯平台）
    if (!WTSRegisterSessionNotification(g_hWnd, NOTIFY_FOR_THIS_SESSION)) {
        DWORD err = GetLastError();
        OutputDebugStringA(("WTSRegisterSessionNotification failed, error:" +
            std::to_string(err) + "\n").c_str());
    }

    return true;
}

void CleanupWindowPlat() {
    if (g_hWnd) { DestroyWindow(g_hWnd); g_hWnd = nullptr; }
    if (!g_className.empty())
        UnregisterClass(g_className.c_str(), GetModuleHandle(NULL));
}
RECT GetWorkAreaPlat() {
    RECT rc{};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);
    return rc;
}

// ═══════════════════════════════════════════════════════════
// 7. 纹理加载（从资源）
// ═══════════════════════════════════════════════════════════
static IWICImagingFactory* GetWICFactory() {
    static IWICImagingFactory* factory = nullptr;
    if (!factory) {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return nullptr;
    }
    return factory;
}

TextureHandle LoadTextureFromResource(int resourceId,
    const wchar_t* resourceType,
    int* out_width, int* out_height) {
    if (!g_pd3dDevice) return nullptr;

    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), resourceType);
    if (!hRes) {
        OutputDebugStringA("FindResource failed\n");
        return nullptr;
    }

    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return nullptr;

    DWORD dataSize = SizeofResource(NULL, hRes);
    void* pData = LockResource(hData);
    if (!pData || dataSize == 0) return nullptr;

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream))) return nullptr;

    ULONG bytesWritten = 0;
    stream->Write(pData, dataSize, &bytesWritten);

    LARGE_INTEGER li = { 0 };
    stream->Seek(li, STREAM_SEEK_SET, NULL);

    IWICImagingFactory* factory = GetWICFactory();
    if (!factory) { stream->Release(); return nullptr; }

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromStream(stream, NULL,
        WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr)) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return nullptr;

    // 转 BGRA
    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) { frame->Release(); return nullptr; }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { converter->Release(); frame->Release(); return nullptr; }

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);

    UINT stride = width * 4;
    std::vector<BYTE> pixels(stride * height);
    hr = converter->CopyPixels(NULL, stride, (UINT)pixels.size(), pixels.data());
    converter->Release();
    frame->Release();
    if (FAILED(hr)) return nullptr;

    // 创建 D3D11 纹理
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels.data();
    sub.SysMemPitch = stride;

    ID3D11Texture2D* texture = nullptr;
    hr = g_pd3dDevice->CreateTexture2D(&desc, &sub, &texture);
    if (FAILED(hr) || !texture) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &srv);
    texture->Release();

    if (out_width)  *out_width = (int)width;
    if (out_height) *out_height = (int)height;

    return srv;
}

// platform_windows.cpp
void OpenUrlInBrowser(const std::string& url) {
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// 辅助函数：使用 WIC 解码图片并缩放到指定尺寸，返回 RGBA 像素数据
std::vector<unsigned char> DecodeAndScaleImage(const std::vector<unsigned char>& imageData, int targetWidth, int targetHeight) {
    if (imageData.empty()) return {};

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wicFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        CoUninitialize();
        return {};
    }

    // 创建内存流
    IWICStream* stream = nullptr;
    hr = wicFactory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromMemory(const_cast<BYTE*>(imageData.data()), (DWORD)imageData.size());
        if (SUCCEEDED(hr)) {
            IWICBitmapDecoder* decoder = nullptr;
            hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
            if (SUCCEEDED(hr)) {
                IWICBitmapFrameDecode* frame = nullptr;
                hr = decoder->GetFrame(0, &frame);
                if (SUCCEEDED(hr)) {
                    // 获取原始尺寸
                    UINT origWidth, origHeight;
                    frame->GetSize(&origWidth, &origHeight);

                    // 创建 IWICBitmapScaler 进行缩放
                    IWICBitmapScaler* scaler = nullptr;
                    hr = wicFactory->CreateBitmapScaler(&scaler);
                    if (SUCCEEDED(hr)) {
                        hr = scaler->Initialize(frame, targetWidth, targetHeight, WICBitmapInterpolationModeFant);
                        if (SUCCEEDED(hr)) {
                            // 转换为 RGBA 格式
                            IWICFormatConverter* converter = nullptr;
                            hr = wicFactory->CreateFormatConverter(&converter);
                            if (SUCCEEDED(hr)) {
                                hr = converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                                if (SUCCEEDED(hr)) {
                                    // 复制像素数据
                                    UINT stride = targetWidth * 4;
                                    UINT bufferSize = stride * targetHeight;
                                    std::vector<unsigned char> pixelData(bufferSize);
                                    hr = converter->CopyPixels(nullptr, stride, bufferSize, pixelData.data());
                                    if (SUCCEEDED(hr)) {
                                        converter->Release();
                                        scaler->Release();
                                        frame->Release();
                                        decoder->Release();
                                        stream->Release();
                                        wicFactory->Release();
                                        CoUninitialize();
                                        return pixelData;
                                    }
                                }
                                converter->Release();
                            }
                        }
                        scaler->Release();
                    }
                    frame->Release();
                }
                decoder->Release();
            }
        }
        stream->Release();
    }
    wicFactory->Release();
    CoUninitialize();
    return {};
}

std::vector<unsigned char> ConvertImageToJPEG(const std::vector<unsigned char>& inputImageData) {
    if (inputImageData.empty()) return {};

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        CoUninitialize();
        return {};
    }

    // 创建输入流
    IWICStream* inputStream = nullptr;
    hr = factory->CreateStream(&inputStream);
    if (SUCCEEDED(hr)) {
        hr = inputStream->InitializeFromMemory(const_cast<BYTE*>(inputImageData.data()), (DWORD)inputImageData.size());
        if (SUCCEEDED(hr)) {
            IWICBitmapDecoder* decoder = nullptr;
            hr = factory->CreateDecoderFromStream(inputStream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
            if (SUCCEEDED(hr)) {
                IWICBitmapFrameDecode* frame = nullptr;
                hr = decoder->GetFrame(0, &frame);
                if (SUCCEEDED(hr)) {
                    // 创建 JPEG 编码器
                    IWICBitmapEncoder* encoder = nullptr;
                    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
                    if (SUCCEEDED(hr)) {
                        // 创建输出流（内存）
                        IWICStream* outputStream = nullptr;
                        hr = factory->CreateStream(&outputStream);
                        if (SUCCEEDED(hr)) {
                            hr = outputStream->InitializeFromMemory(nullptr, 0);
                            if (SUCCEEDED(hr)) {
                                hr = encoder->Initialize(outputStream, WICBitmapEncoderNoCache);
                                if (SUCCEEDED(hr)) {
                                    IWICBitmapFrameEncode* frameEncode = nullptr;
                                    hr = encoder->CreateNewFrame(&frameEncode, nullptr);
                                    if (SUCCEEDED(hr)) {
                                        hr = frameEncode->Initialize(nullptr);
                                        if (SUCCEEDED(hr)) {
                                            hr = frameEncode->WriteSource(frame, nullptr);
                                            if (SUCCEEDED(hr)) {
                                                hr = frameEncode->Commit();
                                                if (SUCCEEDED(hr)) {
                                                    hr = encoder->Commit();
                                                    if (SUCCEEDED(hr)) {
                                                        // 获取输出数据大小
                                                        STATSTG stat;
                                                        hr = outputStream->Stat(&stat, STATFLAG_NONAME);
                                                        if (SUCCEEDED(hr)) {
                                                            ULONG size = (ULONG)stat.cbSize.QuadPart;
                                                            std::vector<unsigned char> jpegData(size);
                                                            outputStream->Seek({ 0 }, STREAM_SEEK_SET, nullptr);
                                                            ULONG bytesRead = 0;
                                                            outputStream->Read(jpegData.data(), size, &bytesRead);
                                                            if (bytesRead == size) {
                                                                frameEncode->Release();
                                                                encoder->Release();
                                                                outputStream->Release();
                                                                frame->Release();
                                                                decoder->Release();
                                                                inputStream->Release();
                                                                factory->Release();
                                                                CoUninitialize();
                                                                return jpegData;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        frameEncode->Release();
                                    }
                                }
                                outputStream->Release();
                            }
                        }
                        encoder->Release();
                    }
                    frame->Release();
                }
                decoder->Release();
            }
        }
        inputStream->Release();
    }
    factory->Release();
    CoUninitialize();
    return {};
}
std::string WCharToString(const wchar_t* wstr, UINT codePage = CP_UTF8) {
    if (!wstr || !*wstr) return std::string();

    int len = WideCharToMultiByte(codePage, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return std::string();

    std::string result(len - 1, '\0'); // -1 排除结尾的 L'\0'
    WideCharToMultiByte(codePage, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::string GetFileVersionString()
{
    TCHAR szFullPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, szFullPath, MAX_PATH);  // 获取当前程序路径

    DWORD dwHandle = 0;
    DWORD dwSize = GetFileVersionInfoSize(szFullPath, &dwHandle);
    if (dwSize == 0) return "未知";

    BYTE* pBuffer = new BYTE[dwSize];
    if (!GetFileVersionInfo(szFullPath, dwHandle, dwSize, pBuffer)) {
        delete[] pBuffer;
        return "未知";
    }

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT uLen = 0;
    if (!VerQueryValue(pBuffer, L"\\", (LPVOID*)&pFileInfo, &uLen)) {
        delete[] pBuffer;
        return "未知";
    }

    // 提取版本号各部分
    int major = HIWORD(pFileInfo->dwFileVersionMS);
    int minor = LOWORD(pFileInfo->dwFileVersionMS);
    int build = HIWORD(pFileInfo->dwFileVersionLS);
    int revision = LOWORD(pFileInfo->dwFileVersionLS);

    delete[] pBuffer;

    wchar_t version[64];
    if (revision != 0) { swprintf(version, 64, L"%d.%d.%d.%d", major, minor, build, revision); }
    else { swprintf(version, 64, L"%d.%d.%d", major, minor, build); }

    return WCharToString(version);
}



// 启动新进程（使用当前命令行）
void RestartApplication(){
    if (g_hMutex) { CloseHandle(g_hMutex); g_hMutex = nullptr; }
    LPWSTR cmdLine = GetCommandLineW();
    std::wstring cmd(cmdLine);
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    // 无论启动是否成功，当前进程退出
    ExitProcess(0);
}
TextureHandle LoadAppIcon(int* out_width, int* out_height) {
    return LoadTextureFromResource(IDB_PNG1, L"PNG", out_width, out_height);
}

std::vector<NetAdapterInfo> EnumNetworkAdapters() {
    std::vector<NetAdapterInfo> result;

    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    if (size == 0) return result;

    std::vector<BYTE> buf(size);
    auto* adapter = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
    if (GetAdaptersInfo(adapter, &size) != ERROR_SUCCESS) return result;

    for (auto* p = adapter; p; p = p->Next) {
        std::string ip = p->IpAddressList.IpAddress.String;
        if (ip.empty() || ip == "0.0.0.0") continue;

        NetAdapterInfo info;
        info.ip = ip;
        info.description = p->Description;
        info.hasGateway = (strcmp(p->GatewayList.IpAddress.String, "0.0.0.0") != 0);

        if (p->Type == MIB_IF_TYPE_ETHERNET) info.type = 1;
        else if (p->Type == IF_TYPE_IEEE80211)    info.type = 2;

        result.push_back(std::move(info));
    }
    return result;
}

std::string PathToUtf8(const PathType& path) {
    if (path.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1,
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string buf(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1,
        &buf[0], len, nullptr, nullptr);
    return buf;
}

#endif // PLATFORM_WINDOWS