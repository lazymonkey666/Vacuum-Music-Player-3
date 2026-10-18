#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>


#include <imgui/imgui.h>
#include <platform/platform.h>

/*
 * ==============================================================================
 * 【Vacuum Music Player 主题制作与打包指南】
 * ==============================================================================
 *
 * 一、文件结构与打包
 *
 * 在电脑上新建一个文件夹（比如叫 MyTheme），将你的文件按下面结构放好：
 *
 * MyTheme/
 * ├── theme.json          <-- 必须有！用 AI 生成的主题配置（提示词见下方）
 * ├── background.png      <-- 必须有！你的底图（名字必须是 background.png 或 底图.png）
 * └── icons/              <-- 可选！自定义图标目录
 *     ├── play.svg           （如果想省事，可以直接使用我的仓库 src/resources 里的 svg，
 *     ├── pause.svg           material design 风格的图标会适配大部分主题）
 *     ├── next.svg
 *     ├── prev.svg
 *     └── ...
 *
 * 💡 注意：background.png 建议尺寸比例接近 3:1（例如 1050x350），这样在默认窗口下拉伸变形最小。
 *
 *
 * 二、打包成 ZIP（以 Windows 为例）
 *
 * 1. 打开 MyTheme 文件夹。
 * 2. 【关键】选中里面的所有文件（theme.json、background.png 和 icons 文件夹）。
 * 3. 右键点击选中的文件 -> 选择“发送到” -> “压缩(zipped)文件夹”。
 * 4. 把生成的压缩包重命名为你喜欢的名字，比如 GlassSky.zip。
 *
 * ⚠️ 避坑指南：
 * ❌ 错误：右键压缩了 MyTheme 这个文件夹本身，导致解压后多了一层目录（MyTheme/theme.json）。
 * ✅ 正确：让 theme.json 处于 ZIP 压缩包的最顶层（选中文件 -> 压缩）。
 *
 *
 * 三、安装使用
 *
 * 打开 Vacuum Music Player -> 进入 设置 -> 主题包（Theme Pack） -> 选择你的 zip 文件。
 * 享受新外观！（记得按 Win + I 切换系统深色/浅色模式，测试适配效果）
 *
 * ==============================================================================
 * 【AI Prompt：请直接复制以下内容发给大模型（如 ChatGPT/Claude）】
 * ==============================================================================
 *
 * 你现在是一位专业的 UI 视觉设计师和前端开发。请帮我为一款名为 "Vacuum Music Player"
 * 的 ImGui 音乐播放器生成一个 `theme.json` 主题配置文件。
 *
 * 【核心规则】
 * 1. 如果用户上传了背景图片，请分析该图片的颜色基调，或遵循用户的要求，提取出适合作为主色调
 *    的 Accent（强调色），并生成一套完美契合该图片的浅色（默认）和深色（dark节点）配色。
 * 2. 颜色格式必须使用 Hex 的 `#RRGGBB` 或带透明度的 `#RRGGBBAA`。
 *    **特别注意：由于播放器是毛玻璃/半透明风格的，很多背景和组件的 Alpha 通道（最后两位）
 *    必须调整，以透出底图。**
 * 3. **【关于 style 节点的死命令】**：你的输出必须严格遵守以下规则，否则播放器窗口的观感会严重破坏！
 *    - **允许修改**：所有的 `Rounding`（圆角），你可以根据主题风格自行发挥（例如赛博朋克风可设为 0.0，可爱圆润风可设为 12.0 甚至更高）。
 *    - **绝对禁止修改**：所有的 `BorderSize` 必须为 0.0（保证无边框毛玻璃感），以及 `ItemSpacing` 和 `WindowPadding` 的数值（保证播放器内部排版不乱）。
 *    请基于此规则输出，基础模板如下：
 *   "style": {
 *     "WindowRounding":    12.0,  // 可修改
 *     "FrameRounding":     12.0,  // 可修改
 *     "ChildRounding":     12.0,  // 可修改
 *     "GrabRounding":      12.0,  // 可修改
 *     "TabRounding":       12.0,  // 可修改
 *     "ScrollbarRounding": 12.0,  // 可修改
 *     "WindowBorderSize":  0.0,   // 严禁修改
 *     "FrameBorderSize":   0.0,   // 严禁修改
 *     "PopupBorderSize":   0.0,   // 严禁修改
 *     "ItemSpacingX":      10.0,  // 严禁修改
 *     "ItemSpacingY":      6.0,   // 严禁修改
 *     "WindowPaddingX":    6.0,   // 严禁修改
 *     "WindowPaddingY":    6.0    // 严禁修改
 *   }
 *
 * 【JSON 结构要求】
 * 请输出合法的 JSON，结构如下：
 * {
 *   "id": "给你的主题起一个英文变量名，不能有空格",
 *   "name": "显示的主题名称（中文或英文）",
 *   "version": "1.0",
 *   "author": "AI Generated",
 *   "description": "一句话描述这个主题的风格",
 *   "background": "background.png",
 *   "icons": "icons",
 *   "accent": {
 *     "mode": "custom",
 *     "color": "#提取的主色调，比如 #FF3366"
 *   },
 *   "colors": {
 *     // 这里填入你的浅色（默认）配置。
 *     // 关键提示：
 *     // "WindowBg": 建议使用类似 "#F8FAFCB3" 这种带不透明度(如B3, CC)的浅色，透出背景。
 *     // "Text": 必须使用深色（如 #1A1A1A），确保在浅色背景上能看清。
 *     // "ChildBg", "FrameBg", "Border", "Button" 建议使用带极低透明度的背景，实现无边框效果。
 *   },
 *   "dark": {
 *     "colors": {
 *       // 这里填入深色专属配置。由于底层代码会自动继承浅色的 style 和没写到的颜色，
 *       // 你只需要覆盖关键的颜色（如将 Text 改为浅色，WindowBg 改为深灰半透明）。
 *     }
 *   }
 * }
 *
 * 【颜色 Key 参考列表】（请尽量填充完整，保持视觉统一）
 * Text, TextDisabled, TextSelectedBg, WindowBg, ChildBg, PopupBg, Border, BorderShadow,
 * FrameBg, FrameBgHovered, FrameBgActive, TitleBg, TitleBgActive, TitleBgCollapsed,
 * MenuBarBg, ScrollbarBg, ScrollbarGrab, ScrollbarGrabHovered, ScrollbarGrabActive,
 * CheckMark, SliderGrab, SliderGrabActive, Button, ButtonHovered, ButtonActive,
 * Header, HeaderHovered, HeaderActive, Separator, SeparatorHovered, SeparatorActive,
 * ResizeGrip, ResizeGripHovered, ResizeGripActive, Tab, TabHovered, TabActive,
 * TabUnfocused, TabUnfocusedActive, PlotLines, PlotLinesHovered, PlotHistogram,
 * PlotHistogramHovered, TableHeaderBg, TableBorderStrong, TableBorderLight, TableRowBg,
 * TableRowBgAlt, NavHighlight, NavWindowingHighlight, NavWindowingDimBg, ModalWindowDimBg。
 *
 * ！！：重要内容，不要节约 token 少输出一些颜色 Key，必须输出完整的 JSON。
 * 请直接输出 JSON 代码块。
 * ==============================================================================
 */




// ─────────────────────────────────────────────
// ThemePack 数据结构
// ─────────────────────────────────────────────
struct ThemePackMeta {
    std::string id;          // 唯一标识（安装目录名）
    std::string name;        // 显示名
    std::string version;     // 主题版本，如 "0.1"
    std::string author;
    std::string description;
};

struct ThemePackAccent {
    bool  useSystem = true;                          // 是否用系统强调色
    ImVec4 custom = ImVec4(0.0f, 0.47f, 0.84f, 1.0f); // 自带强调色
};

struct ThemePackStyle {
    float windowRounding = 4.0f;
    float frameRounding = 6.0f;
    float childRounding = 6.0f;
    float grabRounding = 2.0f;
    float tabRounding = 3.0f;
    float scrollbarRounding = 3.0f;
    float windowBorderSize = 0.0f;
    float frameBorderSize = 1.0f;
    float popupBorderSize = 0.0f;
    float itemSpacingX = 8.0f;
    float itemSpacingY = 4.0f;
    float windowPaddingX = 8.0f;
    float windowPaddingY = 8.0f;
};

class ThemePack {
public:
    ThemePackMeta meta;
    ThemePackAccent accent;
    ThemePackStyle styleData;
    std::unordered_map<std::string, ImVec4> colors;   // ImGuiCol 名 -> 颜色

    // 资源路径
    std::string rootDir;          // 主题解压根目录
    std::string backgroundPath;   // 背景图完整路径，可能为空
    std::string iconsDir;         // 图标目录完整路径，可能为空

    bool hasDarkVariant = false; // 标记作者是否提供了深色配置
    std::unordered_map<std::string, ImVec4> colorsDark;
    ThemePackStyle styleDataDark;

    // 纹理（由 Manager 加载）
    TextureHandle backgroundTexture = nullptr;

    bool LoadFromDir(const std::string& dir);
    bool IsValid() const { return !meta.id.empty() && !meta.name.empty(); }

    void ReleaseTextures();
};

// ─────────────────────────────────────────────
// ThemePackManager 单例
// ─────────────────────────────────────────────
class ThemePackManager {
public:
    static ThemePackManager& GetInstance();

    // 扫描 themesDir 下所有子目录，加载为 ThemePack
    size_t ScanDir(const std::string& themesDir);

    // 从 zip 安装到 themesDir，返回安装后的 id（失败返回空串）
    std::string InstallFromZip(const std::string& zipPath, const std::string& themesDir);

    // 卸载
    bool Uninstall(const std::string& id, const std::string& themesDir);

    // 查询
    std::vector<std::string> ListIds() const;
    ThemePack* Get(const std::string& id);

    // 激活（id 为空则取消主题，回到默认样式）
    bool Activate(const std::string& id);
    ThemePack* GetActive() { return Get(m_activeId); }
    const std::string& GetActiveId() const { return m_activeId; }

    // 应用主题到 ImGuiStyle。systemAccent 由调用方传入（Windows: 注册表；Linux: 可固定）
    void ApplyToImGui(const ImVec4& systemAccent,bool isDark);

    // 加载当前激活主题的背景纹理（需要平台提供 LoadTextureFromFile）
    void LoadActiveBackgroundTexture();

    // 清空（切换主题 / 关闭时调用）
    void ClearAll();

    // 从绝对路径的 zip 加载（临时解压到 temp），返回 id；失败返回空串
    std::string LoadFromZipAbsolute(const std::string& zipPath);

    // 当前激活主题的 zip 路径（如果是绝对路径加载的）
    const std::string& GetActiveZipPath() const { return m_activeZipPath; }

    

private:
    std::string m_activeZipPath;   // 记录绝对 zip 路径

private:
    ThemePackManager() = default;
    std::unordered_map<std::string, std::unique_ptr<ThemePack>> m_themes;
    std::string m_activeId;
};