#pragma once
#pragma once
#include "PlaybackStateMachine.h"
#include <string>
#include <vector>
#include <utility>

// 全局状态机（定义在 main.cpp）
extern PlaybackStateMachine g_sm;

// ═══════════════════════════════════════════════════════════
// 播放状态只读视图 —— 所有 UI / Service / 外部模块从这里读
// 生命周期：跟随 g_sm，不持有额外状态
// ═══════════════════════════════════════════════════════════

inline const PlaybackSnapshot& Ps() { return g_sm.Snap(); }

inline bool     PsIsPlaying() { return g_sm.IsPlaying(); }
inline bool     PsIsLoading() { return g_sm.IsLoading(); }
inline uint64_t PsPositionMs() { return g_sm.Snap().positionMs; }
inline uint64_t PsDurationMs() { return g_sm.Snap().durationMs; }
inline float    PsPositionSec() { return PsPositionMs() / 1000.0f; }
inline float    PsDurationSec() { return PsDurationMs() / 1000.0f; }
inline float    PsProgressRatio() {
    auto dur = PsDurationMs();
    return dur > 0 ? (float)PsPositionMs() / (float)dur : 0.0f;
}

// 歌词：解析后的 (时间戳ms, 文本) 列表 —— 从 Ps().lyrics 派生 + 缓存
const std::vector<std::pair<int, std::string>>& PsLyrics();

// 歌词滚动：根据当前 position 计算高亮行（-1 = 还没到第一句）
int PsCurrentLyricIndex();