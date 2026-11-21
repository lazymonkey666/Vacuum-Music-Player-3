#include "PlaybackStateMachine.h"
#include <algorithm>


// ──────────────────────────────────────────────────────────────
// 用户意图
// ──────────────────────────────────────────────────────────────
void PlaybackStateMachine::Play(int index, const std::string& trackId) {
    // 停掉当前播放（异步，不等待）
    if (m_b.requestUnload) m_b.requestUnload();

    // 清掉所有在飞任务
    m_loadTaskId = m_playTaskId = m_pauseTaskId = m_seekTaskId = m_playingTaskId = 0;
    EnterPreparing(index, trackId);
}

void PlaybackStateMachine::Pause() {
    if (m_snap.state != PlayState::Playing) return;
    // 乐观转移：UI 立即响应
    m_snap.state = PlayState::Paused;
    Touch();

    if (m_b.requestPause) m_pauseTaskId = m_b.requestPause(m_snap.index);
}

void PlaybackStateMachine::Resume() {
    if (m_snap.state != PlayState::Paused) return;
    // 乐观转移
    m_snap.state = PlayState::Playing;
    Touch();

    if (m_b.requestPlay) m_playTaskId = m_b.requestPlay(m_snap.index);
}

void PlaybackStateMachine::TogglePlay() {
    if (m_snap.state == PlayState::Playing)  Pause();
    else if (m_snap.state == PlayState::Paused) Resume();
}

void PlaybackStateMachine::Seek(float ratio) {
    if (m_snap.state != PlayState::Playing && m_snap.state != PlayState::Paused) return;
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    uint64_t posMs = (uint64_t)(m_snap.durationMs * ratio);

    // 乐观更新
    m_snap.positionMs = posMs;
    if (m_snap.state == PlayState::Paused) {
        m_snap.state = PlayState::Playing;
    }
    Touch();

    if (m_b.requestSeek) m_seekTaskId = m_b.requestSeek(posMs);
}

void PlaybackStateMachine::Stop() {
    m_loadTaskId = m_playTaskId = m_pauseTaskId = m_seekTaskId = m_playingTaskId = 0;
    if (m_b.requestUnload) m_b.requestUnload();
    m_snap.state = PlayState::Idle;
    m_snap.positionMs = 0;
    Touch();
}

// ──────────────────────────────────────────────────────────────
// 内部状态转移
// ──────────────────────────────────────────────────────────────
void PlaybackStateMachine::EnterPreparing(int index, const std::string& trackId) {
    ++m_gen;

    // ★ 保留旧歌词和旧封面，避免切歌瞬间闪白 + 避免纹理过早释放
    PlaybackSnapshot fresh;
    fresh.lyrics = std::move(m_snap.lyrics);
    fresh.albumArt = std::move(m_snap.albumArt);
    m_snap = std::move(fresh);

    m_snap.state = PlayState::Preparing;
    m_snap.index = index;
    m_snap.trackId = trackId;
    Touch();

    const uint64_t myGen = m_gen;
    m_b.prepare(index, [this, myGen](PrepareResult r) {
        if (myGen != m_gen) return;
        if (m_snap.state != PlayState::Preparing) return;

        if (!r.ok) { EnterFailed(r.error.empty() ? "prepare failed" : r.error); return; }

        m_snap.localPath = r.localPath;
        m_snap.title = r.title;
        m_snap.artist = r.artist;
        m_snap.album = r.album;
        m_snap.lyrics = r.lyrics;      // ← 新歌词覆盖旧歌词
        m_snap.albumArt = r.albumArt;    // ← 新封面覆盖旧封面（可能为空）
        m_snap.durationMs = r.durationMs;
        EnterLoading();
        });
}

void PlaybackStateMachine::EnterLoading() {
    m_snap.state = PlayState::Loading;
    Touch();

    if (m_b.requestLoad)
        m_loadTaskId = m_b.requestLoad(m_snap.localPath, m_snap.index);
}

void PlaybackStateMachine::EnterPlaying() {
    m_snap.state = PlayState::Playing;
    Touch();
}

void PlaybackStateMachine::EnterPaused() {
    m_snap.state = PlayState::Paused;
    Touch();
}

void PlaybackStateMachine::EnterFailed(const std::string& err) {
    m_snap.state = PlayState::Failed;
    m_snap.errorMsg = err;
    Touch();
    if (onPlayFailed) onPlayFailed();
}

void PlaybackStateMachine::Touch() {
    ++m_snap.revision;
    if (onStateChanged) onStateChanged();
}

// ──────────────────────────────────────────────────────────────
// 后端事件
// ──────────────────────────────────────────────────────────────
void PlaybackStateMachine::OnBackendLoaded(uint64_t taskId, bool ok, uint64_t durMs, const std::string& err) {
    if (taskId != m_loadTaskId) return;
    m_loadTaskId = 0;
    if (m_snap.state != PlayState::Loading) return;

    if (!ok) { EnterFailed(err.empty() ? "load failed" : err); return; }
    if (durMs > 0) m_snap.durationMs = durMs;

    // 加载完成立刻播放
    if (m_b.requestPlay) m_playTaskId = m_b.requestPlay(m_snap.index);
}

void PlaybackStateMachine::OnBackendPlayed(uint64_t taskId, uint64_t durMs, int trackIndex) {  
    if (taskId != m_playTaskId) return;
    m_playingTaskId = m_playTaskId;
    m_playTaskId = 0;
    if (durMs > 0) m_snap.durationMs = durMs;
    if (trackIndex >= 0) m_snap.index = trackIndex;   // ★ 兜底同步
    EnterPlaying();
}

void PlaybackStateMachine::OnBackendPaused(uint64_t taskId, bool ok) {
    if (taskId != m_pauseTaskId) return;
    m_pauseTaskId = 0;

    if (!ok && m_snap.state == PlayState::Paused) {
        // 回滚
        m_snap.state = PlayState::Playing;
        Touch();
    }
}

void PlaybackStateMachine::OnBackendSeeked(uint64_t taskId, uint64_t posMs) {
    if (taskId != m_seekTaskId) return;
    m_seekTaskId = 0;
    m_snap.positionMs = posMs;
    Touch();
}

void PlaybackStateMachine::OnBackendEnded(uint64_t taskId) {
    if (m_snap.state != PlayState::Playing) return;
    if (taskId != 0 && taskId != m_playingTaskId) return;

    m_playingTaskId = 0;
    m_snap.state = PlayState::Idle;
    m_snap.positionMs = m_snap.durationMs;
    Touch();
    if (onTrackEnded) onTrackEnded();
}

void PlaybackStateMachine::UpdatePosition(uint64_t posMs) {
    if (m_snap.positionMs == posMs) return;   // 无变化不刷 revision
    m_snap.positionMs = posMs;
    // 注意：不算状态转移，所以不调 Touch()，避免 UI 每帧重建
}