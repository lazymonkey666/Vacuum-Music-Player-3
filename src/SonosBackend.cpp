#include "AsyncBackendBase.h"
#include <SonosController.h>
#include <atomic>
#include <mutex>
#include <thread>

extern uint64_t GetAudioFileDurationMs(const std::string& utf8Path);

class SonosBackend : public AsyncBackendBase {
    // ★ SonosController 私有成员，不再是全局 g_sonos
    SonosController m_controller;

    // 播放状态
    uint64_t m_durationMs = 0;
    bool     m_sonosLoaded = false;
    bool     m_lastPlaying = false;

    // 设备管理（异步搜索）
    std::atomic<bool> m_searching{ false };
    std::atomic<bool> m_hasNewResult{ false };
    std::mutex        m_devMtx;
    std::vector<PlaybackDeviceInfo> m_devices;
    std::atomic<bool> m_connected{ false };

public:
    const char* Id() const override { return "sonos"; }
    const char* DisplayName() const override { return "Sonos 音响"; }

    bool Init() override {
        if (!m_controller.Initialize()) {
            OutputDebugStringA("[Sonos] controller init failed\n");
            return false;
        }
        StartWorker();
        return true;
    }

    void Shutdown() override {
        StopWorker();
        if (m_sonosLoaded) {
            if (auto p = m_controller.GetPlayer()) p->Stop();
            m_sonosLoaded = false;
        }
        m_connected.store(false);
    }

    // ═══════════════════════════════════════════════════════
    // 设备管理（★ 主程序通过此接口访问，不接触 SonosController）
    // ═══════════════════════════════════════════════════════
    bool SupportsDeviceManagement() const override { return true; }

    void StartDeviceSearch() override {
        if (m_searching.exchange(true)) return;
        std::thread([this]() {
            std::vector<DeviceEntry> raw;
            try { raw = m_controller.SearchDevices(); }
            catch (...) { raw.clear(); }

            std::lock_guard<std::mutex> lock(m_devMtx);
            m_devices.clear();
            for (auto& d : raw) {
                PlaybackDeviceInfo info;
                info.id = d.friendlyName;
                info.displayName = d.friendlyName;
                info.location = d.location;
                m_devices.push_back(std::move(info));
            }
            m_hasNewResult.store(true);
            m_searching.store(false);
            }).detach();
    }

    bool IsSearchingDevices() const override { return m_searching.load(); }

    std::vector<PlaybackDeviceInfo> PollFoundDevices() override {
        if (!m_hasNewResult.exchange(false)) return {};
        std::lock_guard<std::mutex> lock(m_devMtx);
        return m_devices;   // 拷贝返回（设备列表很小）
    }

    bool ConnectToDevice(const std::string& location) override {
        if (!m_controller.ConnectToDevice(location)) return false;
        m_connected.store(true);
        return true;
    }

    bool IsDeviceConnected() const override { return m_connected.load(); }

protected:
    void Execute(const Command& cmd) override {
        switch (cmd.type) {
        case CommandType::Load:   DoLoad(cmd);   break;
        case CommandType::Unload: DoUnload(cmd); break;
        case CommandType::Play:   DoPlay(cmd);   break;
        case CommandType::Pause:  DoPause(cmd);  break;
        case CommandType::Stop:   DoStop(cmd);   break;
        case CommandType::Seek:   DoSeek(cmd);   break;
        }
    }

    void PollIdleState() override {
        if (!m_sonosLoaded) return;
        auto player = m_controller.GetPlayer();
        if (!player) return;

        uint64_t pos = m_controller.GetPosition();
        SetPositionMs(pos);

        SONOS::ElementList vars;
        if (player->GetTransportInfo(vars)) {
            std::string state = vars.GetValue("CurrentTransportState");
            bool nowPlaying = (state == "PLAYING");
            if (m_lastPlaying && !nowPlaying) {
                if (!IsAtEnd() && pos + 2000 >= GetDurationMs()) {
                    SetAtEnd(true);
                    SetPlaying(false);
                    EmitIdleEvent(AsyncEventType::BackendEnded, pos, GetDurationMs());
                }
            }
            SetPlaying(nowPlaying);
            m_lastPlaying = nowPlaying;
        }
    }

private:
    void DoLoad(const Command& cmd) {
        if (m_sonosLoaded) {
            if (auto p = m_controller.GetPlayer()) p->Stop();
            m_sonosLoaded = false;
        }
        SetLoaded(false); SetPlaying(false); SetAtEnd(false);
        SetPositionMs(0); SetDurationMs(0);
        m_lastPlaying = false;

        std::string utf8 = PathToUtf8(cmd.path);
        std::string baseName = utf8.substr(utf8.find_last_of("/\\") + 1);

        m_durationMs = GetAudioFileDurationMs(utf8);
        if (m_durationMs == 0) {
            auto p = MakePayload(cmd);
            p.errorMsg = "cannot determine duration";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        m_controller.PlayFile(baseName);
        if (auto p = m_controller.GetPlayer())
            p->SetPlayMode(SONOS::PlayMode_NORMAL);

        m_sonosLoaded = true;
        SetDurationMs(m_durationMs);
        SetLoaded(true);

        auto p = MakePayload(cmd);
        p.durationMs = m_durationMs;
        EmitBackendEvent(AsyncEventType::BackendLoaded, std::move(p));
    }

    void DoUnload(const Command& cmd) {
        if (m_sonosLoaded) {
            if (auto p = m_controller.GetPlayer()) p->Stop();
        }
        m_sonosLoaded = false;
        m_lastPlaying = false;
        SetLoaded(false); SetPlaying(false); SetAtEnd(false);
        SetPositionMs(0); SetDurationMs(0);
        EmitBackendEvent(AsyncEventType::BackendUnloaded, MakePayload(cmd));
    }

    void DoPlay(const Command& cmd) {
        if (!m_sonosLoaded) { EmitError(cmd, "not loaded"); return; }
        auto p = m_controller.GetPlayer();
        if (!p) { EmitError(cmd, "no player"); return; }
        p->Play();
        SetPlaying(true); SetAtEnd(false);
        auto payload = MakePayload(cmd);
        payload.positionMs = GetPositionMs();
        payload.durationMs = GetDurationMs();
        EmitBackendEvent(AsyncEventType::BackendPlayed, std::move(payload));
    }

    void DoPause(const Command& cmd) {
        if (!m_sonosLoaded) { EmitError(cmd, "not loaded"); return; }
        if (auto p = m_controller.GetPlayer()) p->Pause();
        SetPlaying(false);
        auto payload = MakePayload(cmd);
        payload.positionMs = GetPositionMs();
        EmitBackendEvent(AsyncEventType::BackendPaused, std::move(payload));
    }

    void DoStop(const Command& cmd) {
        if (!m_sonosLoaded) { EmitError(cmd, "not loaded"); return; }
        if (auto p = m_controller.GetPlayer()) p->Stop();
        SetPlaying(false); SetPositionMs(0); SetAtEnd(false);
        EmitBackendEvent(AsyncEventType::BackendStopped, MakePayload(cmd));
    }

    void DoSeek(const Command& cmd) {
        if (!m_sonosLoaded) { EmitError(cmd, "not loaded"); return; }
        m_controller.SetPlayPos(cmd.seekMs);
        SetPositionMs(cmd.seekMs); SetAtEnd(false);
        auto payload = MakePayload(cmd);
        payload.positionMs = cmd.seekMs;
        payload.durationMs = GetDurationMs();
        EmitBackendEvent(AsyncEventType::BackendSeeked, std::move(payload));
    }
};

std::unique_ptr<IPlaybackBackend> CreateSonosBackend() {
    return std::make_unique<SonosBackend>();
}