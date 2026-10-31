#include "SettingItem.h"
#include "manageconfig.h"
#include <string>
#include <MusicSource.h>   // ★ 需要完整定义（AddSettings 调用）

// ── 全局（非源）设置变量保持不变 ──
static int         g_acrylic_opacity = 128;
static std::string g_amllAddress = "ws://localhost:11144/";
static std::string g_themeStyle = "System";
static bool        g_enableSnapToEdge = true;
static std::string g_playmode = "local";
static bool        g_enable_cache_limit = true;
static int         g_cache_limit_mb = 2048;
static bool        g_enable_lyric_translate_split = true;
static std::string g_activeThemeZip = "";

SettingWindow::SettingWindow()
    : cfg(ManageConfig::GetInstance())
{
    ManageConfig::GetInstance().Load();

    // 播放来源 radio 从注册表动态生成（源显示名 → 实际 id）
    std::vector<std::pair<std::string, std::string>> PlayModeOptions;
    for (const auto& d : MusicSourceRegistry::GetInstance().GetDescriptors())
        PlayModeOptions.emplace_back(d.displayName, d.id);
    if (PlayModeOptions.empty()) {
        PlayModeOptions = { {"本地播放", "local"}, {"在线（网易云）", "online"} };
    }

    // ── 通用 ──
    SettingCategory general("通用");
    general.items.push_back(std::make_unique<SettingItemRadio>(
        g_playmode, "play_mode", "播放来源", PlayModeOptions));
    general.items.push_back(std::make_unique<SettingItem<bool>>(
        g_enable_cache_limit, "enable_cache_limit", "开启缓存限制"));
    general.items.push_back(std::make_unique<SettingItem<int>>(
        g_cache_limit_mb, "cache_limit_mb", "缓存大小限制(MB)", 512, 8192));
    categories_.push_back(std::move(general));

    // ── 歌词 ──
    SettingCategory lyric("歌词");
    lyric.items.push_back(std::make_unique<SettingItemText>(
        g_amllAddress, "amll_address", "AMLL 音乐播放器地址"));
    lyric.items.push_back(std::make_unique<SettingItem<bool>>(
        g_enable_lyric_translate_split, "enable_lyric_translate_split",
        "允许在连接 AMLL 后自动分割翻译\n中文歌建议关闭，英文歌保持开启"));
    categories_.push_back(std::move(lyric));

    // ── 外观 ──
    SettingCategory appearance("外观");
    appearance.items.push_back(std::make_unique<SettingItemText>(
        g_activeThemeZip, "active_theme_zip", "主题压缩包路径", "*.zip"));
    appearance.items.push_back(std::make_unique<SettingItem<int>>(
        g_acrylic_opacity, "acrylic_opacity", "亚克力透明度", 1, 255));
    appearance.items.push_back(std::make_unique<SettingItem<bool>>(
        g_enableSnapToEdge, "enable_snap_to_edge", "启用窗口贴边停靠"));
    {
        std::vector<std::pair<std::string, std::string>> themeOptions = {
            {"深色", "Dark"}, {"浅色", "Light"}, {"跟随系统", "System"}
        };
        appearance.items.push_back(std::make_unique<SettingItemRadio>(
            g_themeStyle, "theme_style", "主题风格", themeOptions));
    }
    categories_.push_back(std::move(appearance));

    // ── 关于 ──
    SettingCategory about("关于");
    about.items.push_back(std::make_unique<SettingItemAbout>("about", "关于"));
    categories_.push_back(std::move(about));

    for (auto& catAll : categories_)
        for (auto& item : catAll.items) item->load();
}

// ═══════════════════════════════════════════════════════════
// ★ 首次打开设置时，为每个注册源生成"播放源：{displayName}"分类
// ═══════════════════════════════════════════════════════════
void SettingWindow::BuildSourceCategories() {
    if (m_sourcesBuilt) return;
    m_sourcesBuilt = true;

    // 插入点：通用 (index 0) 之后
    int insertAt = 1;

    for (const auto& d : MusicSourceRegistry::GetInstance().GetDescriptors()) {
        // 临时构造实例 → 调用 AddSettings 取设置项 → 销毁实例
        auto instance = d.creator();
        if (!instance) continue;

        std::vector<std::unique_ptr<SettingItemBase>> items;
        instance->AddSettings(items);
        instance.reset();

        if (items.empty()) continue;

        // 标题自动补全："播放源：" + 源的显示名
        SettingCategory cat("播放源：" + d.displayName);
        for (auto& item : items) {
            item->load();                       // 双保险（AddSettings 里已从 config 同步）
            cat.items.push_back(std::move(item));
        }

        categories_.insert(categories_.begin() + insertAt, std::move(cat));
        m_sourceSlots.push_back({ d.id, d.displayName, insertAt });
        insertAt++;
    }
}

void SettingWindow::render() {
    if (!open_) return;

    BuildSourceCategories();       // ★ 懒构建（幂等，只跑一次）

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(700, 230), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("设置", &open_, ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    // 当前生效的源（从 config 的 play_mode 判断）
    std::string activeId = ManageConfig::GetInstance()
        .GetValue<std::string>("play_mode", "local");

    ImGui::BeginChild("Categories", ImVec2(150, 0), true);
    for (int i = 0; i < (int)categories_.size(); ++i) {
        // 判断是否是激活源的分类
        bool isActive = false;
        for (const auto& slot : m_sourceSlots) {
            if (slot.categoryIndex == i && slot.id == activeId) {
                isActive = true;
                break;
            }
        }

        std::string label = categories_[i].name;
        if (isActive) label += ">";

        if (ImGui::Selectable(label.c_str(), selectedCategory_ == i))
            selectedCategory_ = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Settings",
        ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), true);
    if (selectedCategory_ >= 0 && selectedCategory_ < (int)categories_.size()) {
        auto& cat = categories_[selectedCategory_];
        ImGui::Text("【%s】", cat.name.c_str());
        ImGui::Separator();
        for (auto& item : cat.items) { item->render(); ImGui::Spacing(); }
        ImGui::Spacing();
    }
    ImGui::EndChild();

    // 保存按钮（不变）
    float bw = 100.0f, bh = ImGui::GetFrameHeight();
    float sp = ImGui::GetStyle().ItemSpacing.x;
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::SetCursorScreenPos(ImVec2(
        wp.x + ws.x - bw - sp - 10, wp.y + ws.y - bh - 10));
    if (ImGui::Button("保存设置", ImVec2(bw, bh))) {
        for (auto& catAll : categories_)
            for (auto& item : catAll.items) item->save();
        ManageConfig::GetInstance().Save();
        SaveAndRestart();
    }
    ImGui::End();
}