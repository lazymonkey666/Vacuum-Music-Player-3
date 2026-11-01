#include "AsyncBackendBase.h"
#include <miniaudio.h>

class MiniaudioBackend : public AsyncBackendBase {
    // ★ 全部私有成员，不再是全局
    ma_engine m_engine{};
    ma_sound  m_sound{};
    bool      m_engineInited = false;
    bool      m_soundLoaded = false;
    uint64_t  m_totalFrames = 0;

public:
    const char* Id() const override { return "miniaudio"; }
    const char* DisplayName() const override { return "本机扬声器"; }

    bool Init() override {
        if (m_engineInited) return true;
        if (ma_engine_init(nullptr, &m_engine) != MA_SUCCESS) {
            OutputDebugStringA("[Miniaudio] engine init failed\n");
            return false;
        }
        m_engineInited = true;
        StartWorker();
        return true;
    }

    void Shutdown() override {
        StopWorker();
        if (m_soundLoaded) {
            ma_sound_uninit(&m_sound);
            m_soundLoaded = false;
        }
        if (m_engineInited) {
            ma_engine_uninit(&m_engine);
            m_engineInited = false;
        }
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
        if (!m_soundLoaded) return;
        ma_uint64 cur = ma_sound_get_time_in_milliseconds(&m_sound);
        SetPositionMs(cur);
        if (!IsAtEnd() && ma_sound_at_end(&m_sound) == MA_TRUE) {
            SetAtEnd(true);
            SetPlaying(false);
            EmitIdleEvent(AsyncEventType::BackendEnded, GetPositionMs(), GetDurationMs());
        }
    }

private:
    void DoLoad(const Command& cmd) {
        if (m_soundLoaded) { ma_sound_uninit(&m_sound); m_soundLoaded = false; }
        SetLoaded(false); SetPlaying(false); SetAtEnd(false);
        SetPositionMs(0); SetDurationMs(0);
        m_totalFrames = 0;

        if (!m_engineInited) {
            auto p = MakePayload(cmd);
            p.errorMsg = "engine not initialized";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        if (MA_SOUND_INIT_FROM_FILE(&m_engine, cmd.path.c_str(),
            MA_SOUND_FLAG_STREAM, nullptr, nullptr,
            &m_sound) != MA_SUCCESS) {
            auto p = MakePayload(cmd);
            p.errorMsg = "ma_sound_init_from_file failed";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }
        m_soundLoaded = true;

        float sec = 0.0f;
        ma_uint64 frames = 0;
        ma_sound_get_length_in_seconds(&m_sound, &sec);
        ma_sound_get_length_in_pcm_frames(&m_sound, &frames);
        if (sec <= 0.01f || frames == 0) {
            ma_sound_uninit(&m_sound); m_soundLoaded = false;
            auto p = MakePayload(cmd);
            p.errorMsg = "invalid audio";
            EmitBackendEvent(AsyncEventType::BackendLoadFailed, std::move(p));
            return;
        }

        m_totalFrames = frames;
        SetDurationMs((uint64_t)(sec * 1000.0));
        SetLoaded(true);

        auto p = MakePayload(cmd);
        p.durationMs = (uint64_t)(sec * 1000.0);
        EmitBackendEvent(AsyncEventType::BackendLoaded, std::move(p));
    }

    void DoUnload(const Command& cmd) {
        if (m_soundLoaded) {
            ma_sound_stop(&m_sound);
            ma_sound_uninit(&m_sound);
            m_soundLoaded = false;
        }
        SetLoaded(false); SetPlaying(false); SetAtEnd(false);
        SetPositionMs(0); SetDurationMs(0);
        m_totalFrames = 0;
        EmitBackendEvent(AsyncEventType::BackendUnloaded, MakePayload(cmd));
    }

    void DoPlay(const Command& cmd) {
        if (!m_soundLoaded) { EmitError(cmd, "not loaded"); return; }
        if (ma_sound_start(&m_sound) != MA_SUCCESS) {
            EmitError(cmd, "ma_sound_start failed"); return;
        }
        SetPlaying(true); SetAtEnd(false);
        auto p = MakePayload(cmd);
        p.positionMs = GetPositionMs();
        p.durationMs = GetDurationMs();
        EmitBackendEvent(AsyncEventType::BackendPlayed, std::move(p));
    }

    void DoPause(const Command& cmd) {
        if (!m_soundLoaded) { EmitError(cmd, "not loaded"); return; }
        ma_sound_stop(&m_sound);
        SetPlaying(false);
        auto p = MakePayload(cmd);
        p.positionMs = GetPositionMs();
        EmitBackendEvent(AsyncEventType::BackendPaused, std::move(p));
    }

    void DoStop(const Command& cmd) {
        if (!m_soundLoaded) { EmitError(cmd, "not loaded"); return; }
        ma_sound_stop(&m_sound);
        ma_sound_seek_to_pcm_frame(&m_sound, 0);
        SetPlaying(false); SetPositionMs(0); SetAtEnd(false);
        EmitBackendEvent(AsyncEventType::BackendStopped, MakePayload(cmd));
    }

    void DoSeek(const Command& cmd) {
        if (!m_soundLoaded || m_totalFrames == 0) { EmitError(cmd, "not loaded"); return; }
        uint64_t dur = GetDurationMs();
        if (dur == 0) { EmitError(cmd, "duration unknown"); return; }
        double ratio = std::clamp((double)cmd.seekMs / (double)dur, 0.0, 1.0);
        ma_uint64 frame = (ma_uint64)(ratio * m_totalFrames);
        if (ma_sound_seek_to_pcm_frame(&m_sound, frame) != MA_SUCCESS) {
            EmitError(cmd, "seek failed"); return;
        }
        SetPositionMs(cmd.seekMs); SetAtEnd(false);
        auto p = MakePayload(cmd);
        p.positionMs = cmd.seekMs;
        p.durationMs = dur;
        EmitBackendEvent(AsyncEventType::BackendSeeked, std::move(p));
    }
};

std::unique_ptr<IPlaybackBackend> CreateMiniaudioBackend() {
    return std::make_unique<MiniaudioBackend>();
}