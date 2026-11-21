#pragma once
#include <cstdint>
#include <string>
#include <vector>     
#include <functional>

// ═══════════════════════════════════════════════════════════════
// 播放状态机：只管"一条音频的生命周期"
//   Idle → Preparing → Loading → Playing ⇄ Paused → Idle
//   任意状态 → Failed
// ═══════════════════════════════════════════════════════════════

enum class PlayState {
    Idle, Preparing, Loading, Playing, Paused, Failed,
};

struct PlaybackSnapshot {
    PlayState    state = PlayState::Idle;
    int          index = -1;
    std::string  trackId;

    // 元数据（Preparing 完成后填）
    std::string  localPath;
    std::string  title, artist, album, lyrics;
    std::vector<unsigned char> albumArt;

    uint64_t     durationMs = 0;
    uint64_t     positionMs = 0;
    std::string  errorMsg;

    uint64_t     revision = 0;    // 每次变化 +1，UI 靠它判断是否刷新
};

// prepare 返回的详情
struct PrepareResult {
    bool         ok = false;
    std::string  localPath;
    std::string  error;
    std::string  title, artist, album, lyrics;
    std::vector<unsigned char> albumArt;
    uint64_t     durationMs = 0;
};

class PlaybackStateMachine {
public:
    // ═══════════════════════════════════════════════════════════
    // Bridges：把"后端/资源层"和状态机解耦
    // ═══════════════════════════════════════════════════════════
    struct Bridges {
        // 资源准备：缓存检查 / 下载 / 读标签 / 封面 —— 完成后回调
        std::function<void(int index,
            std::function<void(PrepareResult)> onDone)> prepare;

        // 后端操作：fire-and-forget，结果通过 OnBackendXxx 回来
        // 返回 taskId（后端分配，用于匹配回调）
        std::function<uint64_t(const std::string& localPath, int index)> requestLoad;
        std::function<uint64_t(int index)>         requestPlay;
        std::function<uint64_t(int index)>         requestPause;
        std::function<uint64_t(uint64_t posMs)>    requestSeek;
        std::function<void()>                      requestUnload;   // 切歌前清理
    };
    void SetBridges(Bridges b) { m_b = std::move(b); }

    // ═══════════════════════════════════════════════════════════
    // 用户意图（主线程调用）
    // ═══════════════════════════════════════════════════════════
    void Play(int index, const std::string& trackId);
    void Pause();
    void Resume();
    void TogglePlay();
    void Seek(float ratio);
    void Stop();

    // ═══════════════════════════════════════════════════════════
    // 后端事件（主线程调用，taskId 用于匹配）
    // ═══════════════════════════════════════════════════════════
    void OnBackendLoaded(uint64_t taskId, bool ok, uint64_t durMs, const std::string& err);
    void OnBackendPlayed(uint64_t taskId, uint64_t durMs, int trackIndex = -1);
    void OnBackendPaused(uint64_t taskId, bool ok);
    void OnBackendSeeked(uint64_t taskId, uint64_t posMs);
    void OnBackendEnded(uint64_t taskId);

    // 主循环每帧调一次（不算状态转移）
    void UpdatePosition(uint64_t posMs);

    // ═══════════════════════════════════════════════════════════
    // 查询
    // ═══════════════════════════════════════════════════════════
    const PlaybackSnapshot& Snap() const { return m_snap; }
    bool  IsPlaying() const { return m_snap.state == PlayState::Playing; }
    bool  IsLoading() const {
        return m_snap.state == PlayState::Preparing
            || m_snap.state == PlayState::Loading;
    }
    uint64_t Gen() const { return m_gen; }

    // ═══════════════════════════════════════════════════════════
    // 信号（主程序订阅）
    // ═══════════════════════════════════════════════════════════
    std::function<void()> onTrackEnded;     // 自然结束 → 主程序算 next → Play()
    std::function<void()> onPlayFailed;     // 失败 → 主程序决定重试/跳过
    std::function<void()> onStateChanged;   // 任何状态变化

private:
    void EnterPreparing(int index, const std::string& trackId);
    void EnterLoading();
    void EnterPlaying();
    void EnterPaused();
    void EnterFailed(const std::string& err);
    void Touch();

    PlaybackSnapshot m_snap;
    Bridges          m_b;

    uint64_t m_gen = 0;   // 只保护 prepare 阶段
    uint64_t m_loadTaskId = 0;
    uint64_t m_playTaskId = 0;
    uint64_t m_pauseTaskId = 0;
    uint64_t m_seekTaskId = 0;
    uint64_t m_playingTaskId = 0;   // 进入 Playing 后记住，用于校验 Ended
};