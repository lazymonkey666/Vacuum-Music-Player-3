#pragma once
#include "PlaybackService.h"
#include "PlaybackBackend.h"   // PlaybackDeviceInfo
#include <SonosController.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class SonosService : public IPlaybackService {
public:
    const char* Id() const override { return "sonos"; }
    const char* DisplayName() const override { return "Sonos 音响"; }

    bool Init() override;
    void Shutdown() override;

    // 窗口
    bool HasWindow() const override { return true; }
    void ShowWindow() override { m_windowOpen.store(true); }
    void RenderWindow() override;

    // ★ 供 SonosBackend 访问
    SonosController* GetController() {
        return m_controller ? m_controller.get() : nullptr;
    }
    bool IsConnected() const { return m_connected.load(); }
    std::string GetConnectedLocation() const { return m_connectedLocation; }

private:
    SonosService() = default;
    friend std::unique_ptr<IPlaybackService> CreateSonosService();

    bool EnsureController();
    void EnsureHttpServer();
    void DoStartSearch();
    bool DoConnectDevice(const std::string& location);

    std::unique_ptr<SonosController> m_controller;
    bool m_httpStarted = false;

    std::atomic<bool> m_connected{ false };
    std::string        m_connectedLocation;

    // 搜索
    std::atomic<bool> m_searching{ false };
    std::atomic<bool> m_hasNewResult{ false };
    std::mutex        m_devMtx;
    std::vector<PlaybackDeviceInfo> m_devices;

    // UI
    std::atomic<bool> m_windowOpen{ false };
    int               m_selectedIdx = -1;
};

// 工厂（构造 private，所以在 .cpp 里定义）
std::unique_ptr<IPlaybackService> CreateSonosService();