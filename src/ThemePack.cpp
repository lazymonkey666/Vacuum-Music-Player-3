#include "ThemePack.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <platform/platform.h> // 引入平台基础设施（Utf8ToPath, ExtractZipFile 等）
#include <zip_file.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

bool ExtractZipFile_CPP(const std::string& zipPath, const std::string& destDir) {
    try {
        // 1. 使用 miniz-cpp 打开 zip 文件
        // 注意：zipPath 如果是 UTF-8 包含中文，miniz-cpp 内部直接调用 fopen，可能会有问题。
        // 最好这里先转成宽字符，但 miniz-cpp 的构造函数只收 std::string。
        // （如果这里报错打不开中文路径，建议用平台 API 转一下 WideCharToMultiByte 成本地 GBK，或者直接用 Windows 的 PowerShell 算了）
        miniz_cpp::zip_file file(zipPath);

        // ==========================================
        // ★ 关键修改：使用 Utf8ToPath 转换为 PathType 
        // ==========================================
        std::filesystem::path destPath = std::filesystem::path(Utf8ToPath(destDir));

        std::error_code ec;
        std::filesystem::create_directories(destPath, ec);
        if (ec) return false;

        // 3. 遍历 zip 内的所有条目
        for (const auto& name : file.namelist()) {
            if (!name.empty() && name.back() == '/') {
                continue;
            }

            // ★ 关键修改：zip 内部的文件名也是 UTF-8，同样转 PathType
            std::filesystem::path outputPath = destPath / std::filesystem::path(Utf8ToPath(name));

            std::filesystem::path parentPath = outputPath.parent_path();
            if (!parentPath.empty()) {
                std::filesystem::create_directories(parentPath, ec);
            }

            std::string data = file.read(name);

            // ★ 关键修改：因为 outputPath 是宽字符路径（Windows下），
            // MSVC 的 std::ofstream 原生支持接收 std::filesystem::path，直接传即可。
            std::ofstream outFile(outputPath, std::ios::binary);
            if (outFile) {
                outFile.write(data.data(), data.size());
            }
        }
        return true;
    }
    catch (...) {
        return false;
    }
}

// ─────────────────────────────────────────────
// ImGuiCol 名称映射（覆盖 main.cpp 中使用的所有项）
// ─────────────────────────────────────────────
static const std::unordered_map<std::string, ImGuiCol_> kColorNameMap = {
    {"Text",                    ImGuiCol_Text},
    {"TextDisabled",            ImGuiCol_TextDisabled},
    {"WindowBg",                ImGuiCol_WindowBg},
    {"ChildBg",                 ImGuiCol_ChildBg},
    {"PopupBg",                 ImGuiCol_PopupBg},
    {"Border",                  ImGuiCol_Border},
    {"BorderShadow",            ImGuiCol_BorderShadow},
    {"FrameBg",                 ImGuiCol_FrameBg},
    {"FrameBgHovered",          ImGuiCol_FrameBgHovered},
    {"FrameBgActive",           ImGuiCol_FrameBgActive},
    {"TitleBg",                 ImGuiCol_TitleBg},
    {"TitleBgActive",           ImGuiCol_TitleBgActive},
    {"TitleBgCollapsed",        ImGuiCol_TitleBgCollapsed},
    {"MenuBarBg",               ImGuiCol_MenuBarBg},
    {"ScrollbarBg",             ImGuiCol_ScrollbarBg},
    {"ScrollbarGrab",           ImGuiCol_ScrollbarGrab},
    {"ScrollbarGrabHovered",    ImGuiCol_ScrollbarGrabHovered},
    {"ScrollbarGrabActive",     ImGuiCol_ScrollbarGrabActive},
    {"CheckMark",               ImGuiCol_CheckMark},
    {"SliderGrab",              ImGuiCol_SliderGrab},
    {"SliderGrabActive",        ImGuiCol_SliderGrabActive},
    {"Button",                  ImGuiCol_Button},
    {"ButtonHovered",           ImGuiCol_ButtonHovered},
    {"ButtonActive",            ImGuiCol_ButtonActive},
    {"Header",                  ImGuiCol_Header},
    {"HeaderHovered",           ImGuiCol_HeaderHovered},
    {"HeaderActive",            ImGuiCol_HeaderActive},
    {"Separator",               ImGuiCol_Separator},
    {"SeparatorHovered",        ImGuiCol_SeparatorHovered},
    {"SeparatorActive",         ImGuiCol_SeparatorActive},
    {"ResizeGrip",              ImGuiCol_ResizeGrip},
    {"ResizeGripHovered",       ImGuiCol_ResizeGripHovered},
    {"ResizeGripActive",        ImGuiCol_ResizeGripActive},
    {"Tab",                     ImGuiCol_Tab},
    {"TabHovered",              ImGuiCol_TabHovered},
    {"TabActive",               ImGuiCol_TabActive},
    {"TabUnfocused",            ImGuiCol_TabUnfocused},
    {"TabUnfocusedActive",      ImGuiCol_TabUnfocusedActive},
    {"PlotLines",               ImGuiCol_PlotLines},
    {"PlotLinesHovered",        ImGuiCol_PlotLinesHovered},
    {"PlotHistogram",           ImGuiCol_PlotHistogram},
    {"PlotHistogramHovered",    ImGuiCol_PlotHistogramHovered},
    {"TableHeaderBg",           ImGuiCol_TableHeaderBg},
    {"TableBorderStrong",       ImGuiCol_TableBorderStrong},
    {"TableBorderLight",        ImGuiCol_TableBorderLight},
    {"TableRowBg",              ImGuiCol_TableRowBg},
    {"TableRowBgAlt",           ImGuiCol_TableRowBgAlt},
    {"TextSelectedBg",          ImGuiCol_TextSelectedBg},
    {"NavHighlight",            ImGuiCol_NavHighlight},
    {"NavWindowingHighlight",   ImGuiCol_NavWindowingHighlight},
    {"NavWindowingDimBg",       ImGuiCol_NavWindowingDimBg},
    {"ModalWindowDimBg",        ImGuiCol_ModalWindowDimBg},
};

// ─────────────────────────────────────────────
// 辅助：解析颜色（hex 字符串 或 浮点数组）
// ─────────────────────────────────────────────
static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool ParseHexColor(const std::string& s, ImVec4& out) {
    if (s.empty() || s[0] != '#') return false;
    std::string h = s.substr(1);

    if (h.size() == 4) {
        h = { h[0],h[0], h[1],h[1], h[2],h[2], h[3],h[3] };
    }
    else if (h.size() == 3) {
        h = { h[0],h[0], h[1],h[1], h[2],h[2] };
    }

    if (h.size() != 6 && h.size() != 8) return false;

    int r = HexVal(h[0]) * 16 + HexVal(h[1]);
    int g = HexVal(h[2]) * 16 + HexVal(h[3]);
    int b = HexVal(h[4]) * 16 + HexVal(h[5]);
    int a = 255;
    if (h.size() == 8) {
        a = HexVal(h[6]) * 16 + HexVal(h[7]);
    }
    if (r < 0 || g < 0 || b < 0 || a < 0) return false;

    out = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    return true;
}

static bool ParseColor(const json& v, ImVec4& out) {
    if (v.is_string()) {
        return ParseHexColor(v.get<std::string>(), out);
    }
    if (v.is_array() && v.size() >= 3) {
        out.x = v[0].get<float>();
        out.y = v[1].get<float>();
        out.z = v[2].get<float>();
        out.w = (v.size() >= 4) ? v[3].get<float>() : 1.0f;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────
// ThemePack
// ─────────────────────────────────────────────
bool ThemePack::LoadFromDir(const std::string& dir) {
    fs::path root = fs::path(Utf8ToPath(dir));
    fs::path jsonPath = root / "theme.json";

    if (!fs::exists(jsonPath)) {
        return false;
    }

    try {
        std::ifstream f(jsonPath);
        json j;
        f >> j;

        meta.name = j.value("name", "");
        meta.version = j.value("version", "0.0");
        meta.author = j.value("author", "");
        meta.description = j.value("description", "");

        meta.id = j.value("id", "");
        if (meta.id.empty()) {
            meta.id = PathToUtf8(root.filename());
        }

        if (meta.name.empty()) meta.name = meta.id;

        // 背景
        std::string bgName = j.value("background", "");
        if (!bgName.empty()) {
            fs::path bgPath = root / fs::path(Utf8ToPath(bgName));
            if (fs::exists(bgPath) && fs::is_regular_file(bgPath)) {
                backgroundPath = PathToUtf8(bgPath);
            }
        }
        else {
            const char* cands[] = { "background.png", "bg.png", "background.jpg" };
            for (auto c : cands) {
                fs::path p = root / fs::path(Utf8ToPath(c));
                if (fs::exists(p)) {
                    backgroundPath = PathToUtf8(p);
                    break;
                }
            }
        }

        // 图标目录
        std::string iconDirName = j.value("icons", "icons");
        fs::path iconPath = root / fs::path(Utf8ToPath(iconDirName));
        if (fs::exists(iconPath) && fs::is_directory(iconPath)) {
            iconsDir = PathToUtf8(iconPath);
        }

        // 强调色
        if (j.contains("accent") && j["accent"].is_object()) {
            auto& a = j["accent"];
            std::string mode = a.value("mode", "system");
            accent.useSystem = (mode == "system");
            if (a.contains("color")) {
                ParseColor(a["color"], accent.custom);
            }
        }

        // 颜色表
        if (j.contains("colors") && j["colors"].is_object()) {
            for (auto& [key, val] : j["colors"].items()) {
                ImVec4 c;
                if (ParseColor(val, c)) {
                    colors[key] = c;
                }
            }
        }

        // 样式
        if (j.contains("style") && j["style"].is_object()) {
            auto& s = j["style"];
            auto& d = styleData;
            d.windowRounding = s.value("WindowRounding", d.windowRounding);
            d.frameRounding = s.value("FrameRounding", d.frameRounding);
            d.childRounding = s.value("ChildRounding", d.childRounding);
            d.grabRounding = s.value("GrabRounding", d.grabRounding);
            d.tabRounding = s.value("TabRounding", d.tabRounding);
            d.scrollbarRounding = s.value("ScrollbarRounding", d.scrollbarRounding);
            d.windowBorderSize = s.value("WindowBorderSize", d.windowBorderSize);
            d.frameBorderSize = s.value("FrameBorderSize", d.frameBorderSize);
            d.popupBorderSize = s.value("PopupBorderSize", d.popupBorderSize);
            d.itemSpacingX = s.value("ItemSpacingX", d.itemSpacingX);
            d.itemSpacingY = s.value("ItemSpacingY", d.itemSpacingY);
            d.windowPaddingX = s.value("WindowPaddingX", d.windowPaddingX);
            d.windowPaddingY = s.value("WindowPaddingY", d.windowPaddingY);
        }
        if (j.contains("dark") && j["dark"].is_object()) {
            hasDarkVariant = true;

            // 默认继承浅色的配置
            colorsDark = colors;
            styleDataDark = styleData;

            auto& darkNode = j["dark"];

            // 覆盖深色颜色
            if (darkNode.contains("colors") && darkNode["colors"].is_object()) {
                for (auto& [key, val] : darkNode["colors"].items()) {
                    ImVec4 c;
                    if (ParseColor(val, c)) {
                        colorsDark[key] = c;
                    }
                }
            }

            // 覆盖深色样式
            if (darkNode.contains("style") && darkNode["style"].is_object()) {
                auto& s = darkNode["style"];
                auto& d = styleDataDark;
                d.windowRounding = s.value("WindowRounding", d.windowRounding);
                d.frameRounding = s.value("FrameRounding", d.frameRounding);
                d.childRounding = s.value("ChildRounding", d.childRounding);
                d.grabRounding = s.value("GrabRounding", d.grabRounding);
                d.tabRounding = s.value("TabRounding", d.tabRounding);
                d.scrollbarRounding = s.value("ScrollbarRounding", d.scrollbarRounding);
                d.windowBorderSize = s.value("WindowBorderSize", d.windowBorderSize);
                d.frameBorderSize = s.value("FrameBorderSize", d.frameBorderSize);
                d.popupBorderSize = s.value("PopupBorderSize", d.popupBorderSize);
                d.itemSpacingX = s.value("ItemSpacingX", d.itemSpacingX);
                d.itemSpacingY = s.value("ItemSpacingY", d.itemSpacingY);
                d.windowPaddingX = s.value("WindowPaddingX", d.windowPaddingX);
                d.windowPaddingY = s.value("WindowPaddingY", d.windowPaddingY);
            }
        }
        rootDir = dir;
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

void ThemePack::ReleaseTextures() {
    if (backgroundTexture) {
        ReleaseTexture(backgroundTexture);
        backgroundTexture = nullptr;
    }
}

// ─────────────────────────────────────────────
// ThemePackManager
// ─────────────────────────────────────────────
ThemePackManager& ThemePackManager::GetInstance() {
    static ThemePackManager inst;
    return inst;
}

size_t ThemePackManager::ScanDir(const std::string& themesDir) {
    ClearAll();
    fs::path base = fs::path(Utf8ToPath(themesDir));
    if (!fs::exists(base) || !fs::is_directory(base)) return 0;

    size_t count = 0;
    for (auto& entry : fs::directory_iterator(base)) {
        if (!entry.is_directory()) continue;

        std::string dirStr = PathToUtf8(entry.path());

        auto pack = std::make_unique<ThemePack>();
        if (pack->LoadFromDir(dirStr)) {
            std::string id = pack->meta.id;
            m_themes[id] = std::move(pack);
            ++count;
        }
    }
    return count;
}

std::string ThemePackManager::InstallFromZip(const std::string& zipPath,
    const std::string& themesDir) {
    fs::path zip = fs::path(Utf8ToPath(zipPath));
    if (!fs::exists(zip)) return {};

    std::string stem = PathToUtf8(zip.stem());
    fs::path tmpDir = fs::path(Utf8ToPath(themesDir)) / ("__tmp_" + stem);

    std::error_code ec;
    fs::remove_all(tmpDir, ec);

    std::string tmpStr = PathToUtf8(tmpDir);

    // 调 platform.h 的静默解压
    if (!ExtractZipFile_CPP(zipPath, tmpStr)) {
        fs::remove_all(tmpDir, ec);
        return {};
    }

    // 在解压结果里找 theme.json（可能在根目录，也可能在唯一子目录里）
    fs::path themeRoot;
    if (fs::exists(tmpDir / "theme.json")) {
        themeRoot = tmpDir;
    }
    else {
        for (auto& e : fs::directory_iterator(tmpDir)) {
            if (e.is_directory() && fs::exists(e.path() / "theme.json")) {
                themeRoot = e.path();
                break;
            }
        }
    }

    if (themeRoot.empty()) {
        fs::remove_all(tmpDir, ec);
        return {};
    }

    std::string rootStr = PathToUtf8(themeRoot);

    ThemePack tmp;
    if (!tmp.LoadFromDir(rootStr)) {
        fs::remove_all(tmpDir, ec);
        return {};
    }

    std::string id = tmp.meta.id;
    fs::path finalDir = fs::path(Utf8ToPath(themesDir)) / id;

    // 覆盖安装
    fs::remove_all(finalDir, ec);
    fs::rename(themeRoot, finalDir, ec);

    // 清临时目录
    fs::remove_all(tmpDir, ec);

    // 加载进内存
    std::string finalStr = PathToUtf8(finalDir);

    auto pack = std::make_unique<ThemePack>();
    if (!pack->LoadFromDir(finalStr)) return {};
    m_themes[id] = std::move(pack);

    return id;
}

bool ThemePackManager::Uninstall(const std::string& id, const std::string& themesDir) {
    auto it = m_themes.find(id);
    if (it != m_themes.end()) {
        it->second->ReleaseTextures();
        m_themes.erase(it);
    }
    if (m_activeId == id) m_activeId.clear();

    fs::path dir = fs::path(Utf8ToPath(themesDir)) / id;
    std::error_code ec;
    return fs::remove_all(dir, ec) > 0;
}

std::vector<std::string> ThemePackManager::ListIds() const {
    std::vector<std::string> out;
    out.reserve(m_themes.size());
    for (auto& [k, v] : m_themes) out.push_back(k);
    return out;
}

ThemePack* ThemePackManager::Get(const std::string& id) {
    auto it = m_themes.find(id);
    return it == m_themes.end() ? nullptr : it->second.get();
}

bool ThemePackManager::Activate(const std::string& id) {
    if (id.empty()) { m_activeId.clear(); return true; }
    if (m_themes.find(id) == m_themes.end()) return false;
    m_activeId = id;
    return true;
}

void ThemePackManager::ApplyToImGui(const ImVec4& systemAccent,bool isDark) {
    ThemePack* t = GetActive();
    if (!t) return;

    ImGuiStyle& style = ImGui::GetStyle();

    bool useDark = isDark && t->hasDarkVariant;
    const auto& targetColors = useDark ? t->colorsDark : t->colors;
    const auto& targetStyle = useDark ? t->styleDataDark : t->styleData;



    // 1. 应用主题颜色（只覆盖 json 中指定的项）
    for (auto& [name, color] : targetColors) {
        auto it = kColorNameMap.find(name);
        if (it != kColorNameMap.end()) {
            style.Colors[it->second] = color;
        }
    }

    // 2. 强调色
    ImVec4 accent = t->accent.useSystem ? systemAccent : t->accent.custom;
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, accent.w);
    style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
    style.Colors[ImGuiCol_SeparatorActive] = accent;
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
    style.Colors[ImGuiCol_ResizeGripActive] = accent;
    style.Colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
    style.Colors[ImGuiCol_TabActive] = accent;
    style.Colors[ImGuiCol_PlotLinesHovered] = accent;
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = accent;
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.70f);

    // 3. 样式
    auto& d = targetStyle;
    style.WindowRounding = d.windowRounding;
    style.FrameRounding = d.frameRounding;
    style.ChildRounding = d.childRounding;
    style.GrabRounding = d.grabRounding;
    style.TabRounding = d.tabRounding;
    style.ScrollbarRounding = d.scrollbarRounding;
    style.WindowBorderSize = d.windowBorderSize;
    style.FrameBorderSize = d.frameBorderSize;
    style.PopupBorderSize = d.popupBorderSize;
    style.ItemSpacing = ImVec2(d.itemSpacingX, d.itemSpacingY);
    style.WindowPadding = ImVec2(d.windowPaddingX, d.windowPaddingY);
}

void ThemePackManager::LoadActiveBackgroundTexture() {
    ThemePack* t = GetActive();
    if (!t) return;
    if (t->backgroundTexture) return;   // 已加载
    if (t->backgroundPath.empty()) return;

    // LoadTextureFromFile 在 platform_windows.cpp / main.cpp 里提供
    extern TextureHandle LoadTextureFromFile(const std::string&);
    t->backgroundTexture = LoadTextureFromFile(t->backgroundPath);
}

void ThemePackManager::ClearAll() {
    for (auto& [k, v] : m_themes) {
        if (v) v->ReleaseTextures();
    }
    m_themes.clear();
    m_activeId.clear();
}

std::string ThemePackManager::LoadFromZipAbsolute(const std::string& zipPath) {
    fs::path zip = fs::path(Utf8ToPath(zipPath));
    if (!fs::exists(zip) || !fs::is_regular_file(zip)) return {};

    // 解压到 temp/themepack_<文件名>/
    fs::path tmpBase = fs::path(Utf8ToPath(GetTempDir())) / "VacuumMusicPlayer/themes";
    fs::create_directories(tmpBase);

    std::string stem = PathToUtf8(zip.stem());
    fs::path dest = tmpBase / stem;

    std::error_code ec;
    fs::remove_all(dest, ec);   // 每次启动重新解压，保证最新

    std::string destStr = PathToUtf8(dest);

    
    if (!ExtractZipFile_CPP(zipPath, destStr)) {
        fs::remove_all(dest, ec);
        return {};
    }

    // 找 theme.json（根目录或唯一子目录）
    fs::path themeRoot;
    if (fs::exists(dest / "theme.json")) {
        themeRoot = dest;
    }
    else {
        for (auto& e : fs::directory_iterator(dest)) {
            if (e.is_directory() && fs::exists(e.path() / "theme.json")) {
                themeRoot = e.path();
                break;
            }
        }
    }
    if (themeRoot.empty()) {
        fs::remove_all(dest, ec);
        return {};
    }

    std::string rootStr = PathToUtf8(themeRoot);

    auto pack = std::make_unique<ThemePack>();
    if (!pack->LoadFromDir(rootStr)) {
        fs::remove_all(dest, ec);
        return {};
    }

    std::string id = pack->meta.id;
    m_themes[id] = std::move(pack);
    m_activeId = id;
    m_activeZipPath = zipPath;
    return id;
}