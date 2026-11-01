#include "SonosService.h"
#include "PlaybackBackend.h"
#include <ManageConfig.h>
#include <imgui/imgui.h>
#include <platform/platform.h>
#include <filesystem>
#include <thread>

// ═══════════════════════════════════════════════════════════
// 生命周期
// ═══════════════════════════════════════════════════════════
bool SonosService::EnsureController() {
    if (!m_controller) {
        m_controller = std::make_unique<SonosController>();
        if (!m_controller->Initialize()) {
            OutputDebugStringA("[SonosService] controller init failed\n");
            m_controller.reset();
            return false;
        }
    }
    return true;
}

bool SonosService::Init() {
    if (!EnsureController()) return false;
    OutputDebugStringA("[SonosService] Init OK\n");
    return true;
}

void SonosService::Shutdown() {
    // ★ 如果当前活跃后端是 sonos，先切回 miniaudio（避免后端悬空）
    auto& backendReg = PlaybackBackendRegistry::GetInstance();
    if (backendReg.ActiveId() == "sonos") {
        backendReg.Activate("miniaudio");
    }

    if (m_controller) {
        if (m_connected.load()) {
            if (auto p = m_controller->GetPlayer()) p->Stop();
        }
        if (m_httpStarted) {
            m_controller->StopHttpServer();
            m_httpStarted = false;
        }
        m_controller.reset();
    }
    m_connected.store(false);
    m_connectedLocation.clear();
    m_searching.store(false);
    m_windowOpen.store(false);
    m_selectedIdx = -1;
}

// ═══════════════════════════════════════════════════════════
// 内部：HTTP 服务器（Sonos 播放前提）
// ═══════════════════════════════════════════════════════════
void SonosService::EnsureHttpServer() {
    if (m_httpStarted || !m_controller) return;

    // ── 音乐文件夹（本地歌曲根目录）──
    std::string musicFolder = ManageConfig::GetInstance()
        .GetValue<std::string>("music_folder", "");
    if (musicFolder.empty()) {
        OutputDebugStringA("[SonosService] music_folder not configured\n");
        return;
    }

    PathType folderW = Utf8ToPath(musicFolder);
    if (folderW.empty() || !std::filesystem::exists(folderW)) {
        OutputDebugStringA("[SonosService] music folder not found\n");
        return;
    }

    // ── 缓存目录（网易云等在线歌曲）──
    std::string cacheUtf8 = GetTempDir() + "Vacuum Music Player/MusicCache/";
    PathType cacheW = Utf8ToPath(cacheUtf8);
    if (cacheW.empty()) {
        OutputDebugStringA("[SonosService] cache dir conversion failed\n");
        return;
    }

    // 确保缓存目录存在
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(cacheW), ec);
    }

    // ── 端口 ──
    int port = FindAvailablePort(8000, 9000);
    if (port <= 0) {
        OutputDebugStringA("[SonosService] no available port\n");
        return;
    }

    // ★ 三参数版本
    if (m_controller->StartHttpServer(folderW, cacheW, port)) {
        m_httpStarted = true;
        OutputDebugStringA(("[SonosService] HTTP on port " +
            std::to_string(port) + "\n").c_str());
    }
    else {
        OutputDebugStringA("[SonosService] StartHttpServer failed\n");
    }
}

// ═══════════════════════════════════════════════════════════
// 内部：设备搜索
// ═══════════════════════════════════════════════════════════
void SonosService::DoStartSearch() {
    if (m_searching.exchange(true)) return;
    if (!EnsureController()) {
        m_searching.store(false);
        return;
    }

    std::thread([this]() {
        std::vector<DeviceEntry> raw;
        try { raw = m_controller->SearchDevices(); }
        catch (const std::exception& e) {
            OutputDebugStringA(("[SonosService] search exception: " +
                std::string(e.what()) + "\n").c_str());
            raw.clear();
        }
        catch (...) { raw.clear(); }

        std::lock_guard<std::mutex> lock(m_devMtx);
        m_devices.clear();
        for (auto& d : raw) {
            PlaybackDeviceInfo info;
            info.id = d.friendlyName.empty() ? d.name : d.friendlyName;
            info.displayName = d.friendlyName.empty() ? d.name : d.friendlyName;
            info.location = d.location;
            m_devices.push_back(std::move(info));
        }
        m_hasNewResult.store(true);
        m_searching.store(false);
        }).detach();
}

// ═══════════════════════════════════════════════════════════
// 内部：连接设备（★ 成功后主动切后端）
// ═══════════════════════════════════════════════════════════
bool SonosService::DoConnectDevice(const std::string& location) {
    if (!EnsureController()) return false;

    if (!m_controller->ConnectToDevice(location)) {
        OutputDebugStringA("[SonosService] ConnectToDevice failed\n");
        return false;
    }
    m_connectedLocation = location;
    m_connected.store(true);
    OutputDebugStringA("[SonosService] device connected\n");

    // 确保 HTTP 服务器
    EnsureHttpServer();

    // ★ 关键：切到 Sonos 播放后端
    if (!PlaybackBackendRegistry::GetInstance().Activate("sonos")) {
        OutputDebugStringA("[SonosService] activate sonos backend failed\n");
        return false;
    }
    OutputDebugStringA("[SonosService] backend switched to sonos\n");
    return true;
}


// ═══════════════════════════════════════════════════════════
// 窗口渲染
// ═══════════════════════════════════════════════════════════
void SonosService::RenderWindow() {
    if (!m_windowOpen.load()) return;

    bool open = m_windowOpen.load();

    // ★ 稍宽一点，容纳"设备列表 + 按钮列"分栏
    ImGui::SetNextWindowSize(ImVec2(430, 230), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Sonos 音响", &open,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        m_windowOpen.store(open);
        return;
    }

    auto& backendReg = PlaybackBackendRegistry::GetInstance();
    bool isActiveBackend = (backendReg.ActiveId() == "sonos");

    // 读设备列表（每帧直读，不用 hasNewResult 标志）
    std::vector<PlaybackDeviceInfo> devicesCopy;
    {
        std::lock_guard<std::mutex> lock(m_devMtx);
        devicesCopy = m_devices;
    }

    // 分栏宽度
    const float rightW = 130.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float leftW = ImGui::GetContentRegionAvail().x - rightW - spacing;

    // ═══════════════════════════════════════════════════════
    // 左侧：设备列表（可滚动）
    // ═══════════════════════════════════════════════════════
    ImGui::BeginChild("##SonosDevList", ImVec2(leftW, 0), true);
    if (m_searching.load()) {
        ImGui::TextDisabled("正在搜索...");
    }
    else if (devicesCopy.empty()) {
        ImGui::TextDisabled("未发现设备");
        ImGui::TextDisabled("点右侧「搜索设备」");
    }
    else {
        ImGui::TextDisabled("发现 %d 个设备", (int)devicesCopy.size());
        ImGui::Separator();
        for (size_t i = 0; i < devicesCopy.size(); ++i) {
            bool sel = (m_selectedIdx == (int)i);
            if (ImGui::Selectable(devicesCopy[i].displayName.c_str(), sel)) {
                m_selectedIdx = (int)i;
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ═══════════════════════════════════════════════════════
    // 右侧：按钮列（垂直排列，宽度占满右栏）
    // ═══════════════════════════════════════════════════════
    ImGui::BeginChild("##SonosBtns", ImVec2(rightW, 0), false);

    // ① 搜索
    if (m_searching.load()) {
        ImGui::BeginDisabled();
        ImGui::Button("搜索中...", ImVec2(-1, 0));
        ImGui::EndDisabled();
    }
    else {
        if (ImGui::Button("搜索设备", ImVec2(-1, 0))) {
            DoStartSearch();
        }
    }

    ImGui::Spacing();

    // ② 连接 / 切回 / 播放中
    bool canConnect = (m_selectedIdx >= 0 && m_selectedIdx < (int)devicesCopy.size());
    if (m_connected.load()) {
        if (isActiveBackend) {
            ImGui::BeginDisabled();
            ImGui::Button("Sonos 播放中", ImVec2(-1, 0));
            ImGui::EndDisabled();
        }
        else {
            if (ImGui::Button("切回此音响", ImVec2(-1, 0))) {
                backendReg.Activate("sonos");
            }
        }
    }
    else if (canConnect) {
        if (ImGui::Button("连接此设备", ImVec2(-1, 0))) {
            DoConnectDevice(devicesCopy[m_selectedIdx].location);
        }
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("连接此设备", ImVec2(-1, 0));
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ③ 回到本机
    if (isActiveBackend) {
        if (ImGui::Button("回到本机", ImVec2(-1, 0))) {
            backendReg.Activate("miniaudio");
        }
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("回到本机", ImVec2(-1, 0));
        ImGui::EndDisabled();
    }

    // ④ 底部状态徽章
    ImGui::Dummy(ImVec2(0, 4));
    if (isActiveBackend) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "● Sonos 输出");
    }
    else if (m_connected.load()) {
        ImGui::TextDisabled("○ 已连接未使用");
    }
    else {
        ImGui::TextDisabled("○ 未连接");
    }

    ImGui::EndChild();

    ImGui::End();
    m_windowOpen.store(open);
}

// ═══════════════════════════════════════════════════════════
// 工厂
// ═══════════════════════════════════════════════════════════
std::unique_ptr<IPlaybackService> CreateSonosService() {
    return std::unique_ptr<SonosService>(new SonosService());
}