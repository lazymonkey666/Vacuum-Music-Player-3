#include "pch.h"
#include "MainUI.h"
#include "SettingItem.h"
#include "MusicSource.h"
#include "PlaybackBackend.h"
#include "PlaybackService.h"
#include "IconManager.h"
#include "ThemePack.h"
#include <IconManager.h>
#include <utils.h>
#include <imgui/imgui.h>
#include <iostream>
#include <algorithm>
#include <PlaybackView.h>
#include <cmath>
#include <AppState.h>
#include "WindowAnimator.h"

// 引用 main.cpp 的核心业务函数
extern void PlayMusicFile(int index);
extern void OnMediaPrev();
extern void OnMediaNext();
extern bool ChangePlayMode();
extern bool seek(float seekRatio);
extern IPlaybackBackend* ActiveBackend();
extern void AutoPlayNextSong();

namespace MainUI {
    // 变量定义
    float g_leftWidthRatio = 0.65f;
    bool g_resizingSplitter = false;
    bool g_showSearchWindow = false;
    char g_searchKeyword[256] = "";
    std::vector<int> g_searchResults;
    int g_currentSearchIndex = -1;
    bool g_needScrollToSong = false;
    int g_targetScrollIndex = -1;
    bool g_NeedScrollToCurrent = false;
    int g_scrollBlockCounter = 0;
    float g_lyricsTargetY = 0.0f;
    float g_lyricsLastScrollY = 0.0f;

    TextureHandle g_prevAlbumArt = nullptr;
    float g_albumArtFadeAlpha = 1.0f;
    bool g_isAlbumArtTransitioning = false;
    std::chrono::steady_clock::time_point g_albumArtTransStartTime;

    ImVec4 ConvertToImVec4(uint32_t color, bool hasAlpha = false) {
        // 0xAARRGGBB -> (A,R,G,B)
        float a = hasAlpha ? ((color >> 24) & 0xFF) / 255.0f : 1.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        return ImVec4(r, g, b, a);
    }


    TextureHandle GetIcon(const std::string& name) {
        auto it = g_iconCache.find(name);
        if (it != g_iconCache.end())
            return it->second;

        std::string path;

        // 优先用主题里的图标
        auto* theme = ThemePackManager::GetInstance().GetActive();
        if (theme && !theme->iconsDir.empty()) {
            std::string themed = theme->iconsDir + "/" + name + ".svg";
            if (std::filesystem::exists(std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(themed.data()), themed.size())))) {
                path = themed;
            }
        }

        // 回落到默认资源
        if (path.empty()) {
            path = "resources/" + name + ".svg";
        }

        auto tex = IconManager::GetInstance().LoadIcon(path.c_str(), 16, 16, g_iconColor);
        g_iconCache[name] = tex;
        return tex;
    }


    bool SelectableWithStyle(const char* label, bool selected, float rounding) {
        // 禁用所有默认背景（选中、悬浮、按下）
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));

        bool is_clicked = ImGui::Selectable(label, selected);

        ImGui::PopStyleColor(3);

        bool is_hovered = ImGui::IsItemHovered();
        bool is_active = ImGui::IsItemActive();   // 鼠标按下状态

        if (selected || is_hovered || is_active) {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            // 动态限制圆角半径
            float item_height = max.y - min.y;
            float actual_rounding = (rounding > item_height * 0.5f) ? (item_height * 0.5f) : rounding;

            // 根据状态选择颜色
            ImU32 col;
            if (selected)
                col = ImGui::GetColorU32(ImGuiCol_Header);
            else if (is_active)
                col = ImGui::GetColorU32(ImGuiCol_HeaderActive);
            else
                col = ImGui::GetColorU32(ImGuiCol_HeaderHovered);

            draw_list->AddRectFilled(min, max, col, actual_rounding);
        }

        return is_clicked;
    }


    void RenderFrame(int counter, SettingWindow& settings) {
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus
        );



        // 获取主窗口大小
        ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        // 如果存在封面纹理，绘制在窗口右侧（230x230）
#if PLATFORM_LINUX
        { //linux需要背景 不然太透明了
            bool isDark = ResolveIsDark(g_dark_mode);

            // 深色：黑灰；浅色：白灰
            float bgR = isDark ? 0.10f : 0.96f;
            float bgG = isDark ? 0.10f : 0.96f;
            float bgB = isDark ? 0.12f : 0.98f;

            // 透明度：g_opacity [0, 255]  alpha [0.0, 1.0]
            float alpha = g_opacity / 255.0f;
            if (alpha < 0.00f) alpha = 0.00f;   // 下限，避免完全看不见
            if (alpha > 1.00f) alpha = 1.00f;

            ImU32 bgColor = ImGui::GetColorU32(ImVec4(bgR, bgG, bgB, alpha));

            // 铺满整个窗口，圆角 0px
            ImVec2 pMin = winPos;
            ImVec2 pMax = ImVec2(winPos.x + winSize.x, winPos.y + winSize.y);
            float rounding = 0.0f;

            draw_list->AddRectFilled(pMin, pMax, bgColor, rounding);

            // 可选：细边框（深浅色适配）
            float borderAlpha = alpha * 0.6f;
            ImU32 borderColor = ImGui::GetColorU32(
                isDark ? ImVec4(1.0f, 1.0f, 1.0f, borderAlpha * 0.15f)
                : ImVec4(0.0f, 0.0f, 0.0f, borderAlpha * 0.20f));
            draw_list->AddRect(pMin, pMax, borderColor, rounding, 0, 1.0f);
        }
#endif
        // 主题背景
        if (auto* theme = ThemePackManager::GetInstance().GetActive()) {
            if (theme->backgroundTexture) {
                draw_list->AddImage(
                    (ImTextureID)theme->backgroundTexture,
                    winPos,
                    ImVec2(winPos.x + winSize.x, winPos.y + winSize.y)
                );
            }
        }
        // ================= 处理专辑图淡入淡出动画 =================
        // ================= 处理专辑图淡入淡出动画 =================
        if (g_isAlbumArtTransitioning) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - g_albumArtTransStartTime).count();

            // 0.35 秒完成过渡，再也不怕卡顿跳帧
            g_albumArtFadeAlpha = elapsed / 0.35f;

            if (g_albumArtFadeAlpha >= 1.0f) {
                g_albumArtFadeAlpha = 1.0f;
                g_isAlbumArtTransitioning = false;
                // 动画结束，彻底释放旧图
                if (g_prevAlbumArt) {
                    ReleaseTexture(g_prevAlbumArt);
                    g_prevAlbumArt = nullptr;
                }
            }
        }

        float imgWidth = 230.0f;
        float imgHeight = 230.0f;
        ImVec2 imgPos(winPos.x + winSize.x - imgWidth, winPos.y);
        ImVec2 imgEndPos(imgPos.x + imgWidth, imgPos.y + imgHeight);

        // 1. 绘制旧图（透明度从 1.0 渐变到 0.0）
        if (g_prevAlbumArt) {
            float alpha = 1.0f - g_albumArtFadeAlpha;
            ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
            draw_list->AddImage((ImTextureID)g_prevAlbumArt, imgPos, imgEndPos, ImVec2(0, 0), ImVec2(1, 1), tint);
        }

        // 2. 绘制新图（透明度从 0.0 渐变到 1.0）
        if (MusicMeta.albumArtSRV) {
            float alpha = g_albumArtFadeAlpha;
            ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
            draw_list->AddImage((ImTextureID)MusicMeta.albumArtSRV, imgPos, imgEndPos, ImVec2(0, 0), ImVec2(1, 1), tint);
        }
        // ==========================================================


        // 左侧区域宽度
        float leftWidth = winSize.x * g_leftWidthRatio;
        float splitterWidth = 6.0f;  // 分割条宽度
        float rightWidth = winSize.x - leftWidth - splitterWidth - (8.0f) * 4;

        // 左侧区域（歌曲列表、按钮、进度条）
        ImGui::BeginChild("LeftPane", ImVec2(leftWidth, 0), true);
        {
            ImGui::BeginChild("MainLayout", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);


            // 设置列表大小（宽度自适应，高度200像素）
            float available_height = ImGui::GetContentRegionAvail().y;
            float progress_height = 24.0f;
            float button_bar_height = ImGui::GetFrameHeightWithSpacing(); // 大约
            float listbox_height = available_height - progress_height - button_bar_height - 8.0f;
            ImVec2 listbox_size(-FLT_MIN, listbox_height);

            ImGui::SetNextItemAllowOverlap();
            if (ImGui::BeginListBox("##MusicList", listbox_size)) {
                bool scrolled = false;
                size_t trackCount = g_source->GetCount();

                // ★ ====================== 懒加载触发机制 ======================
                if (trackCount > 0 && g_source) {
                    // 1. 计算当前视口对应的行区间
                    float itemHeight = ImGui::GetTextLineHeightWithSpacing(); // 单行高度
                    float scrollY = ImGui::GetScrollY();                      // 当前滚动条位置
                    float winHeight = ImGui::GetWindowHeight();               // 列表窗口可见高度

                    int startIndex = (int)(scrollY / itemHeight);
                    int endIndex = (int)((scrollY + winHeight) / itemHeight) + 1;

                    // 2. 上下各多预加载 10 首作为缓冲，防止滚动过快出现空白
                    int preloadStart = (std::max)(0, startIndex - 19);
                    int preloadEnd = (std::min)((int)trackCount, endIndex + 30);

                    // 3. 通知数据源（如 NeteaseSource）去后台拉取该区间的详情
                    // 注：内部已有并发控制和去重，这里每帧调用也不会重复发请求
                    g_source->RequestTrackDetails(preloadStart, preloadEnd);
                }
                // ==============================================================

                for (int i = 0; i < trackCount; ++i) {
                    int t_scrollToSong = -1;
                    const BaseTrackInfo* track = g_source->GetBaseTrack(i);
                    bool is_selected = (CurrentIndex == i);

                    if (g_needScrollToSong && i == g_targetScrollIndex) {
                        ImGui::SetScrollHereY(0.5f);
                        g_needScrollToSong = false;
                        t_scrollToSong = g_targetScrollIndex;
                        g_targetScrollIndex = -1;
                    }

                    // 自动滚动（只滚一次）
                    if (g_NeedScrollToCurrent && !scrolled && i == CurrentIndex) {
                        ImGui::SetScrollHereY(0.5f);
                        g_NeedScrollToCurrent = false;
                        scrolled = true;
                    }

                    // 搜索匹配判断
                    bool isMatch = false;
                    if (strlen(g_searchKeyword) > 0) {
                        std::string kw = g_searchKeyword;
                        // ★ 修改：从接口获取 displayName 替代旧的 only_music_file_name[i]
                        std::string title = track ? track->displayName : "";

                        std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
                        std::transform(title.begin(), title.end(), title.begin(), ::tolower);

                        if (title.find(kw) != std::string::npos) {
                            isMatch = true;
                        }
                    }

                    // ======================
                    // 颜色样式栈（核心修复）
                    // ======================
                    int popCount = 0;

                    // 1. 当前播放项 → 黄色（最高优先级）
                    if (i == t_scrollToSong) {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255)); // 亮黄
                        popCount++;
                    }
                    // 2. 搜索匹配 → 橙色
                    else if (isMatch) {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 140, 0, 255));
                        popCount++;
                    }

                    // 绘制条目
                    if (SelectableWithStyle(track ? track->displayName.c_str() : "No Display Name 无显示名称", is_selected, 6.0f)) {
                        CurrentIndex = i;
                        PlayMusicFile(CurrentIndex);
                    }

                    // 弹出颜色（必须平衡！）
                    if (popCount > 0)
                        ImGui::PopStyleColor(popCount);
                }
                ImGui::EndListBox();
            }
            
            //if (g_pTaskbarList && g_hWnd && counter == 0) {
            //    if (PlayCurrentMs > 0) {
            //        g_pTaskbarList->SetProgressValue(g_hWnd, PlayCurrentMs, PlayMusicLongMs);
            //    }
            //    if (!IsPlaying) {
            //        g_pTaskbarList->SetProgressState(g_hWnd, TBPF_PAUSED);
            //    }
            //    else {
            //        g_pTaskbarList->SetProgressState(g_hWnd, TBPF_NORMAL);
            //    }
            //}

            // ========== 按钮栏（水平排列） =========
            int button_count = 6;
            float availWidth = ImGui::GetContentRegionAvail().x - (8.0f * (button_count - 1));

            if (ImGui::ImageButton("prev_btn", (ImTextureID)GetIcon("prev"), ImVec2(16, 16))) {
                OnMediaPrev();
            }

            ImGui::SameLine();
            if (PsIsPlaying()) {
                // 第一个参数是字符串 ID（必须唯一），第二个参数是纹理 ID，第三个是尺寸
                if (ImGui::ImageButton("pause_btn", (ImTextureID)GetIcon("pause"), ImVec2(16, 16))) {
                    ChangePlayMode();
                }
            }
            else {
                if (ImGui::ImageButton("play_btn", (ImTextureID)GetIcon("play"), ImVec2(16, 16))) {
                    ChangePlayMode();
                }
            }
            ImGui::SameLine();
            if (ImGui::ImageButton("next_btn", (ImTextureID)GetIcon("next"), ImVec2(16, 16))) {
                OnMediaNext();
            }

            ImGui::SameLine();
            // 隐藏/显示窗口按钮
            if (ImGui::ImageButton("hide_btn", (ImTextureID)GetIcon("hide"), ImVec2(16, 16))) {
                WindowAnimator::Get().Toggle();
            }
            ImGui::SameLine();

            // 滚动到当前播放按钮
            if (ImGui::ImageButton("locate_btn", (ImTextureID)GetIcon("locate"), ImVec2(16, 16))) {
                g_NeedScrollToCurrent = true;
            }
            ImGui::SameLine();

            // 播放模式按钮（文字随模式变化）
            const char* modeName = "";
            if (g_PlayOrder == 0) modeName = "order";
            else if (g_PlayOrder == 1) modeName = "round";   // 单曲循环
            else modeName = "random";

            if (ImGui::ImageButton(modeName, (ImTextureID)GetIcon(modeName), ImVec2(16, 16))) {
                g_PlayOrder = (g_PlayOrder + 1) % 3;

                // ★ 切换为随机播放时，重新填充随机播放池
                if (g_PlayOrder == 2) {
                    g_RemainPlaylist.clear();
                    size_t trackCount = g_source->GetCount();
                    for (size_t i = 0; i < trackCount; ++i) {
                        const BaseTrackInfo* track = g_source->GetBaseTrack(i);
                        if (track) {
                            g_RemainPlaylist.push_back(track->id);
                        }
                    }
                }
            }
            ImGui::SameLine();

            // 搜索按钮
            if (ImGui::ImageButton("search_btn", (ImTextureID)GetIcon("search"), ImVec2(16, 16))) {
                g_showSearchWindow = !g_showSearchWindow;
            }
            ImGui::SameLine();
            if (ImGui::ImageButton("more_btn", (ImTextureID)GetIcon("more"), ImVec2(16, 16))) {
                ImGui::OpenPopup("更多选项");
            }
            static bool s_open_sonos_search_window = false;
            if (ImGui::BeginPopup("更多选项")) {
                // 这里可以放置任意控件
                ImGui::Text("额外设置");
                ImGui::Separator();

                if (ImGui::BeginMenu("服务")) {
                    auto& sreg = PlaybackServiceRegistry::GetInstance();
                    const auto& descriptors = sreg.GetDescriptors();

                    if (descriptors.empty()) {
                        ImGui::MenuItem("(无可用服务)", NULL, false, false);
                    }
                    else {
                        for (const auto& d : descriptors) {
                            bool enabled = sreg.IsEnabled(d.id);
                            bool clickable = d.userConfigurable;   // ★ 可点性

                            // ★ 标签补充说明
                            std::string label = d.displayName;
                            if (!clickable) {
                                label += "  (内置)";
                            }
                            else if (enabled && d.id == "amll") {
                                label += "  (已连接)";
                            }

                            // ★ 第 4 个参数 enabled 控制是否可点：
                            //   true  → 正常可点
                            //   false → 灰色不可点
                            if (ImGui::MenuItem(label.c_str(), NULL, enabled, clickable)) {
                                if (enabled) sreg.Disable(d.id);
                                else         sreg.Enable(d.id);
                            }

                            if (ImGui::IsItemHovered()) {
                                if (!clickable) {
                                    ImGui::SetTooltip("始终启用");
                                }
                                else {
                                    ImGui::SetTooltip("ID: %s", d.id.c_str());
                                }
                            }
                            if (enabled) {
                                auto* svc = sreg.GetService(d.id);
                                if (svc && svc->HasWindow()) {
                                    std::string panelLabel = "  打开 " + d.displayName + " 面板";
                                    if (ImGui::MenuItem(panelLabel.c_str())) {
                                        svc->ShowWindow();
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }


                if (ImGui::MenuItem("设置", NULL)) {
                    settings.setOpen(true);
                }
                ImGui::Separator();
                ImGui::EndPopup();
            }
            settings.setSource(g_source.get());
            settings.render();
            PlaybackServiceRegistry::GetInstance().RenderAllWindows();


            ImVec2 progressbar_size(-FLT_MIN, -FLT_MIN);
            // ✅ 新
            ImGui::ProgressBar(PsProgressRatio(), ImVec2(-FLT_MIN, progress_height), "");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                float mouseX = ImGui::GetMousePos().x;
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float seekRatio = (mouseX - itemMin.x) / (itemMax.x - itemMin.x);
                seek(seekRatio);
            }
            std::string progress_text = FormatTime(PsPositionMs()) + " / " + FormatTime(PsDurationMs());

            const auto& ps = Ps();
            std::string now_playing_text = ps.title;
            if (!PsIsLoading()) {
                if (!ps.artist.empty()) now_playing_text += " - " + ps.artist;
                if (!ps.album.empty())  now_playing_text += " [ " + ps.album + " ] ";
            }
            else {
                now_playing_text = GetStatusText();
            }


            // 获取进度条区域
            ImVec2 barMin = ImGui::GetItemRectMin();
            ImVec2 barMax = ImGui::GetItemRectMax();
            float barWidth = barMax.x - barMin.x;

            // 计算右侧文本的宽度（加上右侧边距6px）
            ImVec2 rightTextSize = ImGui::CalcTextSize(progress_text.c_str());
            float rightTextWidth = rightTextSize.x + 6.0f;  // 右侧留6px边距

            // 左侧文本可用宽度 = 总宽度 - 右侧文本宽度 - 边距
            float leftAvailableWidth = barWidth - rightTextWidth - 24.0f;

            std::string display_left_text = now_playing_text;

            // 如果连 "..." 都放不下，直接不显示
            float ellipsisWidth = ImGui::CalcTextSize("...").x;
            if (leftAvailableWidth <= ellipsisWidth) {
                display_left_text = "";
            }
            // 如果左侧文本超长，开始裁剪
            else if (ImGui::CalcTextSize(display_left_text.c_str()).x > leftAvailableWidth) {
                int cut_len = (int)now_playing_text.length();
                float max_text_width = leftAvailableWidth - ellipsisWidth;

                // ★ 从后往前遍历寻找安全截断点，绝对不会死循环
                while (cut_len > 0) {
                    // 安全 UTF-8 边界检查：如果切到了汉字的中间字节，继续往前退
                    while (cut_len > 0 && (now_playing_text[cut_len] & 0xC0) == 0x80) {
                        cut_len--;
                    }

                    std::string sub = now_playing_text.substr(0, cut_len);
                    if (ImGui::CalcTextSize(sub.c_str()).x <= max_text_width) {
                        break; // 找到合适长度了，退出
                    }
                    cut_len--; // 还是太长，继续往前退
                }
                display_left_text = now_playing_text.substr(0, cut_len) + "...";
            }

            // 计算文字垂直居中位置
            float textY = barMin.y + (barMax.y - barMin.y - ImGui::CalcTextSize("A").y) * 0.5f;

            // 绘制
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // ★ 加上物理裁剪，超出进度条左侧区域的文字会被 GPU 直接切掉，绝对不越界
            drawList->PushClipRect(barMin, ImVec2(barMax.x - rightTextWidth - 6.0f, barMax.y), true);

            // 绘制左侧文本（左对齐，左边距6px）
            if (!display_left_text.empty()) {
                drawList->AddText(ImVec2(barMin.x + 6.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), display_left_text.c_str());
            }

            drawList->PopClipRect();

            // 绘制右侧文本（右对齐，右边距6px）
            float rightTextX = barMax.x - rightTextSize.x - 6.0f;
            drawList->AddText(ImVec2(rightTextX, textY), ImGui::GetColorU32(ImGuiCol_Text), progress_text.c_str());
            ImGui::EndChild();

        }
        ImGui::EndChild();
        float paneHeight = ImGui::GetWindowHeight() - (8.0f) * 2;
        // 同一行绘制分割条
        ImGui::SameLine();
        if (g_resizingSplitter) {
            ImGui::PushStyleColor(ImGuiCol_Button, g_accent);
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.05f));
        }
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_accent);
        ImGui::Button("##Splitter", ImVec2(splitterWidth, paneHeight));
        if (ImGui::IsItemActive()) {
            // 正在拖拽分割条
            g_resizingSplitter = true;
            float delta = ImGui::GetIO().MouseDelta.x;
            float newLeftWidth = leftWidth + delta;
            // 限制最小宽度（例如 300px）和最大宽度（例如 winSize.x - 200px）
            newLeftWidth = (std::max)(4.0f, (std::min)(newLeftWidth, winSize.x - (8.0f) * 2 - 4.0f));
            g_leftWidthRatio = newLeftWidth / winSize.x;
        }
        else {
            g_resizingSplitter = false;
        }
        ImGui::PopStyleColor(3);

        // 右侧区域（歌词 + 封面）
        // 右侧区域（歌词 + 封面）
        // 右侧区域（歌词 + 封面）
        ImGui::SameLine();
        ImGui::BeginChild("RightPane", ImVec2(rightWidth, 0), true);
        {
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winSize = ImGui::GetWindowSize();
            ImVec2 mousePos = ImGui::GetMousePos();

            // 手动判断鼠标是否在窗口内
            bool isHovered = (mousePos.x >= winPos.x && mousePos.x <= winPos.x + winSize.x &&
                mousePos.y >= winPos.y && mousePos.y <= winPos.y + winSize.y);

            float wheel = ImGui::GetIO().MouseWheel;
            if (isHovered && wheel != 0.0f)
            {
                g_scrollBlockCounter = 300;
            }


            // 仅大于0时递减，0永远不动
            if (g_scrollBlockCounter > 0)
            {
                g_scrollBlockCounter--;
            }
            // ================================================================================
            const auto& lyrics = PsLyrics();
            if (ImGui::BeginListBox("##LyricsList", ImVec2(-FLT_MIN, -FLT_MIN)))
            {
                if (lyrics.empty())
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "暂无歌词");
                }
                else
                {
                    for (int i = 0; i < (int)lyrics.size(); ++i)
                    {
                        bool isCurrent = (i == g_currentLyricIndex);

                        // 根据是否为当前行选择合适的字体
                        if (isCurrent && g_FontLarge)
                            ImGui::PushFont(g_FontLarge);
                        else if (g_FontNormal)
                            ImGui::PushFont(g_FontNormal);
                        else
                            ImGui::PushFont(ImGui::GetFont()); // 后备字体

                        // ★ 记录当前行的本地 Y 坐标（在画出来之前取）
                        float itemY = ImGui::GetCursorPosY();

                        std::string uniqueLabel = lyrics[i].second + "##" + std::to_string(i);
                        if (SelectableWithStyle(uniqueLabel.c_str(), isCurrent, 6.0f))
                        {
                            uint64_t dur = PsDurationMs();
                            if (dur > 0) {
                                float targetPercent = (float)((double)lyrics[i].first / (double)dur);
                                seek(targetPercent);
                            }
                        }

                        // 关键：恢复字体
                        ImGui::PopFont();

                        // ★ 计算目标滚动位置
                        if (isCurrent)
                        {
                            // 只有在被允许滚动、或者刚切换歌曲/跳转时（g_needScrollToLyric），才更新目标
                            if (g_scrollBlockCounter == 0)
                            {
                                float winHeight = ImGui::GetWindowHeight();
                                // 居中公式：当前行Y - 窗口一半 + 当前行高度一半
                                g_lyricsTargetY = itemY - winHeight * 0.5f + ImGui::GetTextLineHeight() * 0.5f;

                                // 限制不能超过列表的有效滚动范围
                                g_lyricsTargetY = std::clamp(g_lyricsTargetY, 0.0f, ImGui::GetScrollMaxY());

                                g_needScrollToLyric = false;
                            }
                        }
                    }
                }

                // ★ 核心：使用指数平滑算法执行缓动
                if (g_scrollBlockCounter == 0)
                {
                    float currentScrollY = ImGui::GetScrollY();
                    float distance = g_lyricsTargetY - currentScrollY;

                    // 如果距离极微小，直接吸附，不需要渲染了
                    if (std::abs(distance) > 0.05f)
                    {
                        // 这里的 10.0f 是平滑速度（Smooth Time），数值越大跟随越快
                        // 1.0f - exp(-speed * dt) 是标准的帧率无关缓动公式，末期减速极其自然
                        float alpha = 1.0f - std::exp(-10.0f * ImGui::GetIO().DeltaTime);

                        float nextScrollY = currentScrollY + distance * alpha;

                        // 如果逼近程度极度接近，强制贴合（但不用再跳变了，因为距离已经小于 0.05 了）
                        if (std::abs(g_lyricsTargetY - nextScrollY) < 0.1f) {
                            nextScrollY = g_lyricsTargetY;
                        }

                        ImGui::SetScrollY(nextScrollY);
                    }
                }
                else
                {
                    // 用户滚轮时同步目标
                    g_lyricsTargetY = ImGui::GetScrollY();
                }

                ImGui::EndListBox();
            }
        }
        ImGui::EndChild();



        ImGui::End();

        // 搜索窗口（浮动子窗口）
        if (g_showSearchWindow) {
            ImGui::SetNextWindowSizeConstraints(ImVec2(300, 120), ImVec2(300, 120));
            if (ImGui::Begin("搜索歌曲", &g_showSearchWindow, ImGuiWindowFlags_NoCollapse)) {
                // 输入框（实时搜索）
                bool searchChanged = ImGui::InputText("关键词", g_searchKeyword, sizeof(g_searchKeyword));
                if (searchChanged) {
                    // 实时更新搜索结果
                    g_searchResults.clear();
                    std::string keyword = g_searchKeyword;
                    if (!keyword.empty()) {
                        std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
                        for (int i = 0; i < g_source->GetCount(); ++i) {
                            const BaseTrackInfo* track = g_source->GetBaseTrack(i);
                            std::string title = track ? track->displayName : "";
                            std::transform(title.begin(), title.end(), title.begin(), ::tolower);
                            if (title.find(keyword) != std::string::npos) {
                                g_searchResults.push_back(i);
                            }
                        }
                    }
                    g_currentSearchIndex = g_searchResults.empty() ? -1 : 0;
                }
                ImGui::SameLine();
                if (ImGui::Button("清除")) {
                    g_searchKeyword[0] = '\0';
                    g_searchResults.clear();
                    g_currentSearchIndex = -1;
                }

                if (!g_searchResults.empty()) {
                    ImGui::Separator();
                    ImGui::Text("搜索结果 (%zu 项)", g_searchResults.size());

                    // 显示搜索结果数量，并提供“上一个”和“下一个”按钮
                    ImGui::SameLine();
                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float buttonWidth = 80.0f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - buttonWidth * 2 - ImGui::GetStyle().ItemSpacing.x);
                    if (ImGui::Button("上一个", ImVec2(buttonWidth, 0))) {
                        if (!g_searchResults.empty()) {
                            g_currentSearchIndex--;
                            if (g_currentSearchIndex < 0) g_currentSearchIndex = (int)g_searchResults.size() - 1;
                            g_needScrollToSong = true;
                            g_targetScrollIndex = g_searchResults[g_currentSearchIndex];
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("下一个", ImVec2(buttonWidth, 0))) {
                        if (!g_searchResults.empty()) {
                            g_currentSearchIndex++;
                            if (g_currentSearchIndex >= (int)g_searchResults.size()) g_currentSearchIndex = 0;
                            g_needScrollToSong = true;
                            g_targetScrollIndex = g_searchResults[g_currentSearchIndex];
                        }
                    }

                    // 可选：显示当前匹配项序号（不展示具体列表，避免与滚动冲突）
                    ImGui::Text("当前: %d / %d", g_currentSearchIndex + 1, (int)g_searchResults.size());
                }
                else if (strlen(g_searchKeyword) > 0) {
                    ImGui::Text("未找到匹配的歌曲");
                }
            }
            ImGui::End();
        }
        else {
            // 窗口关闭时，清除所有搜索状态（关键词、结果、滚动标志）
            if (g_searchKeyword[0] != '\0') {
                g_searchKeyword[0] = '\0';
                g_searchResults.clear();
                g_currentSearchIndex = -1;
                g_needScrollToSong = false;
                g_targetScrollIndex = -1;
            }
        }

        if (g_source && g_source->HasWindow()) {
            g_source->RenderWindow();
        }

        // 渲染 D3D11
        ImGui::Render();
    }

    void UpdateImGuiStyle() {

        ImGuiStyle& style = ImGui::GetStyle();
        bool isDark = false;
        // 1. 获取当前系统深色模式状态和强调色
        if (g_dark_mode.Get() == "System") {
            isDark = IsDarkModeByRegistry();   // 你之前实现的函数
        }
        else if (g_dark_mode.Get() == "Dark") {
            isDark = true;
        }
        else {
            isDark = false;
        }
        DWORD accentColor = GetAccentColorFromRegistry();
        ImVec4 accent = ConvertToImVec4(accentColor);
        g_accent = accent;
        // 2. 设置基础色板 (深色/浅色)
        if (auto* t = ThemePackManager::GetInstance().GetActive()) { //主题接管
            DWORD accentColor = GetAccentColorFromRegistry();
            ImVec4 accent = ConvertToImVec4(accentColor);
            ThemePackManager::GetInstance().ApplyToImGui(accent, isDark);
            g_accent = t->accent.useSystem ? accent : t->accent.custom;
            return;
        }

        if (isDark) {
            // 深色主题基础色
            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);
            style.Colors[ImGuiCol_InputTextCursor] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 0.94f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.20f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.30f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.30f, 0.10f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.35f, 0.40f, 0.10f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.08f, 0.80f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 0.30f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 0.40f);
            style.Colors[ImGuiCol_CheckMark] = accent;  // 使用强调色
            style.Colors[ImGuiCol_SliderGrab] = accent;
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
            style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.20f, 0.30f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.60f, 0.60f, 0.30f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
            style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.5f, 0.5f, 0.5f, 0.20f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.6f, 0.6f, 0.6f, 0.80f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.40f, 0.40f, 0.80f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_SeparatorActive] = accent;
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.30f, 0.80f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_ResizeGripActive] = accent;
            style.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.12f, 0.86f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(accent.x, accent.y, accent.z, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.14f, 0.97f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
            style.Colors[ImGuiCol_PlotLinesHovered] = accent;
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = accent;
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.70f, 0.70f, 0.80f, 0.70f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
        }
        else {
            // 浅色主题基础色
            style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
            style.Colors[ImGuiCol_InputTextCursor] = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.96f, 0.94f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.20f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.60f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.30f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.85f, 0.85f, 0.87f, 0.10f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.80f, 0.80f, 0.82f, 0.10f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.94f, 0.94f, 0.96f, 0.80f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.94f, 0.94f, 0.96f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.90f, 0.90f, 0.92f, 0.20f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.70f, 0.75f, 0.40f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.65f, 0.40f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.55f, 0.80f);
            style.Colors[ImGuiCol_CheckMark] = accent;
            style.Colors[ImGuiCol_SliderGrab] = accent;
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
            style.Colors[ImGuiCol_Button] = ImVec4(0.90f, 0.90f, 0.90f, 0.30f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.50f, 0.50f, 0.30f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
            style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.5f, 0.5f, 0.5f, 0.60f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.6f, 0.6f, 0.6f, 0.80f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.60f, 0.60f, 0.65f, 0.80f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_SeparatorActive] = accent;
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.70f, 0.70f, 0.75f, 0.80f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_ResizeGripActive] = accent;
            style.Colors[ImGuiCol_Tab] = ImVec4(0.88f, 0.88f, 0.90f, 0.86f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(accent.x, accent.y, accent.z, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.92f, 0.92f, 0.94f, 0.97f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
            style.Colors[ImGuiCol_PlotLinesHovered] = accent;
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = accent;
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.70f, 0.70f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.80f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.50f, 0.50f, 0.55f, 0.70f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
        }

        // 3. 可选的全局样式调整
        style.WindowRounding = 4.0f;
        style.FrameRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 0.0f;
    }


}