#include "AsyncBackendBase.h"
#include "SonosService.h"
#include <ManageConfig.h>
#include <platform/platform.h>

extern uint64_t GetAudioFileDurationMs(const std::string& utf8Path);

// ═══════════════════════════════════════════════════════════
// 把绝对路径转成相对 Sonos HTTP 挂载点的相对路径
//
// 挂载点（SonosController::Impl::StartHttpServer 里配置）：
//   "/"       → music_folder（本地歌曲根目录）
//   "/cache/" → TempDir + "Vacuum Music Player/MusicCache/"（在线歌曲缓存）
//
// 返回值示例：
//   music_folder/专辑A/歌1.mp3          → "专辑A/歌1.mp3"
//   cache/1234567.mp3                  → "cache/1234567.mp3"
//   都不匹配（兜底）                     → "歌1.mp3"
// ═══════════════════════════════════════════════════════════
static std::string BuildSonosRelPath(const PathType& absPath) {
    // 统一分隔符为 '/'
    auto normalize = [](PathType s) {
        std::replace(s.begin(), s.end(), L'\\', L'/');
        return s;
        };

    PathType absW = normalize(absPath);

    // ── 1. 先匹配 music_folder ──
    std::string musicFolderUtf8 = ManageConfig::GetInstance()
        .GetValue<std::string>("music_folder", "");
    if (!musicFolderUtf8.empty()) {
        PathType musicW = normalize(Utf8ToPath(musicFolderUtf8));
        if (!musicW.empty() && musicW.back() != L'/') musicW += L'/';

        if (absW.size() > musicW.size() &&
            absW.compare(0, musicW.size(), musicW) == 0) {
            PathType rel = absW.substr(musicW.size());
            std::string relUtf8 = PathToUtf8(rel);
            OutputDebugStringA(("[SonosBackend] rel path [music] = " + relUtf8 + "\n").c_str());
            return relUtf8;
        }
    }

    // ── 2. 再匹配缓存目录 ──
    std::string cacheUtf8 = GetTempDir() + "Vacuum Music Player/MusicCache/";
    PathType cacheW = normalize(Utf8ToPath(cacheUtf8));
    if (!cacheW.empty() && cacheW.back() != L'/') cacheW += L'/';

    if (absW.size() > cacheW.size() &&
        absW.compare(0, cacheW.size(), cacheW) == 0) {
        PathType rel = absW.substr(cacheW.size());
        std::string relUtf8 = PathToUtf8(rel);
        std::string result = "cache/" + relUtf8;
        OutputDebugStringA(("[SonosBackend] rel path [cache] = " + result + "\n").c_str());
        return result;
    }

    // ── 3. 兜底：只取文件名 ──
    std::string utf8 = PathToUtf8(absPath);
    std::string baseName = utf8.substr(utf8.find_last_of("/\\") + 1);
    OutputDebugStringA(("[SonosBackend] rel path [fallback] = " + baseName + "\n").c_str());
    return baseName;
}


class SonosBackend : public AsyncBackendBase {
    SonosService* m_service = nullptr;   // ★ 不拥有，指向 Service
    uint64_t m_durationMs = 0;
    bool     m_sonosLoaded = false;
    bool     m_lastPlaying = false;

public:
    const char* Id() const override { return "sonos"; }
    const char* DisplayName() const override { return "Sonos 音响"; }

    bool Init() override {
        // ★ 确保 SonosService 启用（懒启用）
        auto& svcReg = PlaybackServiceRegistry::GetInstance();
        if (!svcReg.IsEnabled("sonos")) {
            if (!svcReg.Enable("sonos")) {
                OutputDebugStringA("[SonosBackend] cannot enable SonosService\n");
                return false;
            }
        }

        // ★ 从 Service 拿引用
        m_service = dynamic_cast<SonosService*>(svcReg.GetService("sonos"));
        if (!m_service) {
            OutputDebugStringA("[SonosBackend] SonosService not accessible\n");
            return false;
        }

        // ★ 必须已经连上设备才能启动
        if (!m_service->IsConnected()) {
            OutputDebugStringA("[SonosBackend] no device connected, cannot init\n");
            m_service = nullptr;
            return false;
        }

        StartWorker();
        OutputDebugStringA("[SonosBackend] Init OK\n");
        return true;
    }

    void Shutdown() override {
        StopWorker();
        if (m_sonosLoaded && m_service) {
            if (auto* c = m_service->GetController()) {
                if (auto p = c->GetPlayer()) p->Stop();
            }
            m_sonosLoaded = false;
        }
        m_service = nullptr;
    }

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
        if (!m_sonosLoaded || !m_service) return;
        auto* c = m_service->GetController();
        if (!c) return;
        auto player = c->GetPlayer();
        if (!player) return;

        uint64_t pos = c->GetPosition();
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
        auto* c = m_service ? m_service->GetController() : nullptr;
        if (!c) {
            auto p = MakePayload(cmd);
            p.errorMsg = "sonos service not ready";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        // 停掉旧曲目
        if (m_sonosLoaded) {
            if (auto p = c->GetPlayer()) p->Stop();
            m_sonosLoaded = false;
        }
        SetLoaded(false);
        SetPlaying(false);
        SetAtEnd(false);
        SetPositionMs(0);
        SetDurationMs(0);
        m_lastPlaying = false;

        // ═══════════════════════════════════════════════════════
        // ★ 关键改动：计算相对于 HTTP 挂载点的路径
        // ═══════════════════════════════════════════════════════
        std::string relPath = BuildSonosRelPath(cmd.path);
        if (relPath.empty()) {
            auto p = MakePayload(cmd);
            p.errorMsg = "cannot compute relative path";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        // 校验时长（同时能发现文件损坏/不存在）
        std::string utf8 = PathToUtf8(cmd.path);
        m_durationMs = GetAudioFileDurationMs(utf8);
        if (m_durationMs == 0) {
            auto p = MakePayload(cmd);
            p.errorMsg = "cannot determine duration";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        OutputDebugStringA(("[SonosBackend] DoLoad: " + relPath +
            " (" + std::to_string(m_durationMs) + " ms)\n").c_str());

        // 交给 SonosController 拼 URL 并播放
        if (!c->PlayFile(relPath)) {
            auto p = MakePayload(cmd);
            p.errorMsg = "sonos PlayFile failed";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        m_sonosLoaded = true;
        SetDurationMs(m_durationMs);
        SetLoaded(true);

        auto p = MakePayload(cmd);
        p.durationMs = m_durationMs;
        EmitBackendEvent(AsyncEventType::BackendLoaded, std::move(p));
    }

    void DoUnload(const Command& cmd) {
        auto* c = m_service ? m_service->GetController() : nullptr;
        if (m_sonosLoaded && c) {
            if (auto p = c->GetPlayer()) p->Stop();
        }
        m_sonosLoaded = false; m_lastPlaying = false;
        SetLoaded(false); SetPlaying(false); SetAtEnd(false);
        SetPositionMs(0); SetDurationMs(0);
        EmitBackendEvent(AsyncEventType::BackendUnloaded, MakePayload(cmd));
    }

    void DoPlay(const Command& cmd) {
        auto* c = m_service ? m_service->GetController() : nullptr;
        if (!m_sonosLoaded || !c) { EmitError(cmd, "not loaded"); return; }
        auto p = c->GetPlayer();
        if (!p) { EmitError(cmd, "no player"); return; }
        p->Play();
        SetPlaying(true); SetAtEnd(false);
        auto payload = MakePayload(cmd);
        payload.positionMs = GetPositionMs();
        payload.durationMs = GetDurationMs();
        EmitBackendEvent(AsyncEventType::BackendPlayed, std::move(payload));
    }

    void DoPause(const Command& cmd) {
        auto* c = m_service ? m_service->GetController() : nullptr;
        if (!m_sonosLoaded || !c) { EmitError(cmd, "not loaded"); return; }
        if (auto p = c->GetPlayer()) p->Pause();
        SetPlaying(false);
        auto payload = MakePayload(cmd);
        payload.positionMs = GetPositionMs();
        EmitBackendEvent(AsyncEventType::BackendPaused, std::move(payload));
    }

    void DoStop(const Command& cmd) {
        auto* c = m_service ? m_service->GetController() : nullptr;
        if (!m_sonosLoaded || !c) { EmitError(cmd, "not loaded"); return; }
        if (auto p = c->GetPlayer()) p->Stop();
        SetPlaying(false); SetPositionMs(0); SetAtEnd(false);
        EmitBackendEvent(AsyncEventType::BackendStopped, MakePayload(cmd));
    }

    void DoSeek(const Command& cmd) {
        auto* c = m_service ? m_service->GetController() : nullptr;
        if (!m_sonosLoaded || !c) { EmitError(cmd, "not loaded"); return; }
        c->SetPlayPos(cmd.seekMs);
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