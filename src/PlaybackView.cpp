#include "pch.h"
#include "PlaybackView.h"
#include "utils.h"

const std::vector<std::pair<int, std::string>>& PsLyrics() {
    static std::string                              lastRaw;
    static std::vector<std::pair<int, std::string>> cached;
    const auto& raw = g_sm.Snap().lyrics;
    if (raw != lastRaw) {
        lastRaw = raw;
        cached = ParseLrc(raw);
    }
    return cached;
}

int PsCurrentLyricIndex() {
    const auto& lyrics = PsLyrics();
    if (lyrics.empty()) return -1;
    const uint64_t pos = PsPositionMs();
    int idx = -1;
    for (int i = 0; i < (int)lyrics.size(); ++i) {
        if ((uint64_t)lyrics[i].first <= pos) idx = i;
        else break;
    }
    return idx;
}