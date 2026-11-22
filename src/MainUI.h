#pragma once
#include <imgui/imgui.h>
#include <platform/platform.h>
#include <string>
#include <vector>
#include <SettingItem.h>

class SettingWindow;
struct AlbumArtAsyncResult;

namespace MainUI {
    // ★ 暴露给主程序的纯 UI 状态
    extern float g_leftWidthRatio;        // 左侧宽度比例
    extern bool g_resizingSplitter;       // 是否正在拖拽分割条

    extern bool g_showSearchWindow;       // 是否显示搜索窗口
    extern char g_searchKeyword[256];     // 搜索关键词
    extern std::vector<int> g_searchResults;
    extern int g_currentSearchIndex;
    extern bool g_needScrollToSong;
    extern int g_targetScrollIndex;

    extern bool g_NeedScrollToCurrent;    // 滚动到当前播放项标志

    // 歌词滚动控制
    extern int g_scrollBlockCounter;
    extern float g_lyricsTargetY;
    extern float g_lyricsLastScrollY;

    // 封面淡入淡出动画
    extern TextureHandle g_prevAlbumArt;
    extern float g_albumArtFadeAlpha;
    extern bool g_isAlbumArtTransitioning;
    extern std::chrono::steady_clock::time_point g_albumArtTransStartTime;

    enum StatusCode {
        STATUS_FETCHING_URL = 1,
        STATUS_DOWNLOADING = 2,
        STATUS_WRITING_TAGS = 3,
        STATUS_FETCHING_META = 4,
        STATUS_ERROR = -1,
        STATUS_FETCHING_INFORMATION = 10
    };

    // 渲染主函数
    void RenderFrame(int counter, SettingWindow& settings);

    // 供主程序调用的 UI 操作
    void TriggerScrollToCurrent();
    void ClearSearchState();
    void UpdateImGuiStyle();
    bool ResolveIsDark(const std::string& mode);
    void ApplyTheme(const std::string& mode, int opacity);
    uint32_t GetIconColorForTheme(const std::string& mode);
    void OnAlbumArtReady(AlbumArtAsyncResult* result);
    void ScheduleAlbumArtUpdate();
}