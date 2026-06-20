#pragma once
#include <vector>
#include <string>
#include <functional>
#include <imgui.h>
#include <memory>
#include <sstream>
#include <type_traits>
#include <windows.h>
#include "manageconfig.h"
#include <texture_utils.h>
#include <resource.h>

// forward declare PickFolderDialog (avoid including .cpp). Do not repeat default parameter here.
std::string PickFolderDialog(HWND hwndOwner);
#include <typeinfo>
#include <shellapi.h>  // 在文件开头添加
#pragma comment(lib, "Version.lib")


inline std::string WCharToString(const wchar_t* wstr, UINT codePage = CP_UTF8) {
    if (!wstr || !*wstr) return std::string();

    int len = WideCharToMultiByte(codePage, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return std::string();

    std::string result(len - 1, '\0'); // -1 排除结尾的 L'\0'
    WideCharToMultiByte(codePage, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

inline std::string GetFileVersionString()
{
    TCHAR szFullPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, szFullPath, MAX_PATH);  // 获取当前程序路径

    DWORD dwHandle = 0;
    DWORD dwSize = GetFileVersionInfoSize(szFullPath, &dwHandle);
    if (dwSize == 0) return "未知";

    BYTE* pBuffer = new BYTE[dwSize];
    if (!GetFileVersionInfo(szFullPath, dwHandle, dwSize, pBuffer)) {
        delete[] pBuffer;
        return "未知";
    }

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT uLen = 0;
    if (!VerQueryValue(pBuffer, L"\\", (LPVOID*)&pFileInfo, &uLen)) {
        delete[] pBuffer;
        return "未知";
    }

    // 提取版本号各部分
    int major = HIWORD(pFileInfo->dwFileVersionMS);
    int minor = LOWORD(pFileInfo->dwFileVersionMS);
    int build = HIWORD(pFileInfo->dwFileVersionLS);
    int revision = LOWORD(pFileInfo->dwFileVersionLS);

    delete[] pBuffer;

    wchar_t version[64];
    if (revision != 0) { swprintf(version, 64, L"%d.%d.%d.%d", major, minor, build, revision);  }
    else{ swprintf(version, 64, L"%d.%d.%d", major, minor, build); }
    
    return WCharToString(version);
}

// 基础设置项
struct SettingItemBase {
    virtual ~SettingItemBase() = default;
    virtual void render() = 0;
    virtual void save() = 0;
    virtual void load() = 0;
};


// (移除全局 char 缓冲区，使用 std::string 在实现文件中管理)

inline bool InputTextWithBrowse(const char* label, char* buf, size_t bufSize, const char* buttonLabel = "浏览") {
    bool changed = false;
   
    ImGui::PushID(label);
    ImGui::InputText("##path", buf, bufSize);
    ImGui::SameLine();
    if (ImGui::Button(buttonLabel)) {
        std::string folder = PickFolderDialog(NULL);  // 使用 NULL 或者传入合适的 HWND
        if (!folder.empty()) {
            strncpy_s(buf, bufSize, folder.c_str(), bufSize - 1);
            changed = true;        }
    }
    ImGui::PopID();
    // 可选：显示label（如果不想重复显示，可以把label作为之前的文本）
    
    return changed;
}

// 具体设置项模板
template<typename T>
struct SettingItem : SettingItemBase {
    T& value;
    const char* name; // 保存用的键
    const char* label;
    std::function<void()> onRender = nullptr; // 自定义绘制

    SettingItem(T& val, const char* key, const char* lbl) : value(val), name(key), label(lbl) {}

    // trait to detect if T is streamable to ostream
    template<typename U, typename = void>
    struct is_streamable : std::false_type {};

    template<typename U>
    struct is_streamable<U, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<U>())>> : std::true_type {};

    void render() override {
        if (onRender) {
            onRender();
            return;
        }

        std::string out;

        if constexpr (std::is_arithmetic_v<T>) {
            out = std::to_string(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            out = value;
        } else if constexpr (is_streamable<T>::value) {
            std::ostringstream ss;
            ss << value;
            out = ss.str();
        } else {
            // Fallback: use type name and address
            std::ostringstream ss;
            ss << "<" << typeid(T).name() << ">";
            out = ss.str();
        }

        ImGui::Text("%s: %s", label, out.c_str());
    }
    void save() override { ManageConfig::GetInstance().SetValue(name, value); ManageConfig::GetInstance().Save(); }
    void load() override { /* 后续实现 */ }
};

// 特化 bool
template<>
struct SettingItem<bool> : SettingItemBase {
    bool& value;
    const char* name;
    const char* label;
    SettingItem(bool& val, const char* key, const char* lbl) : value(val), name(key), label(lbl) {}
    void render() override { ImGui::Text(label); ImGui::Checkbox(label, &value); ImGui::Separator();}
    void save() override { ManageConfig::GetInstance().SetValue(name, value); ManageConfig::GetInstance().Save(); }
    void load() override { value = ManageConfig::GetInstance().GetValue<bool>(name, value); }
};

// 特化 int
template<>
struct SettingItem<int> : SettingItemBase {
    int& value;
    const char* name;
    const char* label;
    int minValue, maxValue;
    SettingItem(int& val, const char* key, const char* lbl, int mn = 0, int mx = 100) : value(val), name(key), label(lbl), minValue(mn), maxValue(mx) {}
    void render() override { ImGui::Text(label); std::string tmp = label;  ImGui::SliderInt(("##" + tmp).c_str(), &value, minValue, maxValue); ImGui::Separator(); }
    void save() override { ManageConfig::GetInstance().SetValue(name, value); ManageConfig::GetInstance().Save(); 
    }
    void load() override { value = ManageConfig::GetInstance().GetValue<int>(name, value); }
};
//文件选择框
template<>
struct SettingItem<std::string> : SettingItemBase {
    std::string& value;
    const char* name;
    const char* label;
    SettingItem(std::string& val, const char* key, const char* lbl) : value(val), name(key), label(lbl) {}
    void render() override {
        char buf[512];
        strncpy(buf, value.c_str(), sizeof(buf));
        ImGui::Text(label);
        if (InputTextWithBrowse(label, buf, sizeof(buf))) {
            value = buf;
        }
        ImGui::Separator();
    }
    void save() override { ManageConfig::GetInstance().SetValue(name, value); ManageConfig::GetInstance().Save(); }
    void load() override { value = ManageConfig::GetInstance().GetValue<std::string>(name, value); }
};

struct SettingItemText : SettingItemBase {
    std::string& value;
    const char* name;
    const char* label;
    SettingItemText(std::string& val, const char* key, const char* lbl)
        : value(val), name(key), label(lbl) {}

    void render() override {
        char buf[512];
        strncpy(buf, value.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::Text(label);
        std::string tmp = label;
        if (ImGui::InputText(("##" + tmp).c_str(), buf, sizeof(buf))) {
            value = buf;
            
        }
        ImGui::Separator();
    }

    void save() override {
        ManageConfig::GetInstance().SetValue(name, value);
        ManageConfig::GetInstance().Save();
    }

    void load() override {
        value = ManageConfig::GetInstance().GetValue<std::string>(name, value);
    }
};

// 字符串下拉选择（Combo 框）—— 支持显示名与实际值分离
// 字符串下拉选择（Combo 框）—— 支持显示名与实际值分离
struct SettingItemStringChoice : SettingItemBase {
    std::string& value;                // 实际保存的值（如 "Dark"）
    const char* name;                  // 配置文件中的键名
    const char* label;                 // 设置项显示标题（中文）

    std::vector<std::string> displayOptions;   // 显示给用户的文本（如 "深色"）
    std::vector<std::string> valueOptions;     // 对应的实际值（如 "Dark"）

    // 构造函数：传入 options 映射表，每个 pair 为 {显示文本, 实际值}
    SettingItemStringChoice(std::string& val, const char* key, const char* lbl,
        const std::vector<std::pair<std::string, std::string>>& opts)
        : value(val), name(key), label(lbl) {
        for (const auto& p : opts) {
            displayOptions.push_back(p.first);
            valueOptions.push_back(p.second);
        }
        load(); // 加载配置文件中的值
    }

    void render() override {
        ImGui::Text("%s", label);
        // 根据当前的 value 找到对应的显示索引
        int currentIndex = 0;
        for (size_t i = 0; i < valueOptions.size(); ++i) {
            if (value == valueOptions[i]) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // 每次渲染时动态构建指针数组，确保指针有效
        std::vector<const char*> displayPtrs;
        displayPtrs.reserve(displayOptions.size());
        for (const auto& opt : displayOptions) {
            displayPtrs.push_back(opt.c_str());
        }

        std::string comboId = "##" + std::string(label);
        if (ImGui::Combo(comboId.c_str(), &currentIndex, displayPtrs.data(), static_cast<int>(displayPtrs.size()))) {
            if (currentIndex >= 0 && currentIndex < static_cast<int>(valueOptions.size())) {
                value = valueOptions[currentIndex];
            }
        }
        ImGui::Separator();
    }

    void save() override {
        ManageConfig::GetInstance().SetValue(name, value);
        ManageConfig::GetInstance().Save();
    }

    void load() override {
        std::string loaded = ManageConfig::GetInstance().GetValue<std::string>(name, value);
        // 验证加载的值是否在 valueOptions 中，否则回退到第一个
        bool found = false;
        for (const auto& v : valueOptions) {
            if (loaded == v) {
                found = true;
                break;
            }
        }
        value = found ? loaded : (valueOptions.empty() ? "" : valueOptions[0]);
    }
};

// 单选按钮组（适合选项少的情况）—— 支持显示名与实际值分离
struct SettingItemRadio : SettingItemBase {
    std::string& value;
    const char* name;
    const char* label;
    std::vector<std::string> displayOptions;
    std::vector<std::string> valueOptions;

    SettingItemRadio(std::string& val, const char* key, const char* lbl,
        const std::vector<std::pair<std::string, std::string>>& opts)
        : value(val), name(key), label(lbl) {
        for (const auto& p : opts) {
            displayOptions.push_back(p.first);
            valueOptions.push_back(p.second);
        }
        load();
    }

    void render() override {
        ImGui::Text("%s", label);
        for (size_t i = 0; i < displayOptions.size(); ++i) {
            bool selected = (value == valueOptions[i]);
            if (ImGui::RadioButton(displayOptions[i].c_str(), selected)) {
                value = valueOptions[i];
            }
            if (i != displayOptions.size() - 1) ImGui::SameLine();
        }
        ImGui::Separator();
    }

    void save() override {
        ManageConfig::GetInstance().SetValue(name, value);
        ManageConfig::GetInstance().Save();
    }

    void load() override {
        std::string loaded = ManageConfig::GetInstance().GetValue<std::string>(name, value);
        bool valid = false;
        for (const auto& v : valueOptions) {
            if (loaded == v) { valid = true; break; }
        }
        value = valid ? loaded : (valueOptions.empty() ? "" : valueOptions[0]);
    }
};



// 关于信息设置项（只读，不保存任何配置）
struct SettingItemAbout : SettingItemBase {
    const char* name;   // 配置键名（实际不使用，但为兼容可传空）
    const char* label;  // 显示标题（如 "关于"）

    SettingItemAbout(const char* key, const char* lbl) : name(key), label(lbl) {}

    void render() override {
        static ID3D11ShaderResourceView* iconSRV = nullptr;
        static int iconWidth = 0, iconHeight = 0;

        if (!iconSRV) {
            // 这里的资源 ID 请替换成你的实际 ID，例如 IDB_PNG1
            // 第二个参数是资源类型，通常为 L"PNG"
            bool ok = LoadTextureFromResource(IDB_PNG1, L"PNG", &iconSRV, &iconWidth, &iconHeight);
            if (!ok) {
                // 如果失败，可以尝试其他类型，比如 RT_RCDATA 或 L"PNG"
                ImGui::Text("(图标加载失败)");
            }
        }

        if (iconSRV) {
            float displaySize = 64.0f;
            if (iconWidth > 0 && iconHeight > 0) {
                float ratio = (float)iconWidth / iconHeight;
                ImGui::Image((ImTextureID)iconSRV, ImVec2(displaySize * ratio, displaySize));
            }
            else {
                ImGui::Image((ImTextureID)iconSRV, ImVec2(64, 64));
            }
        }
        std::string version = GetFileVersionString();
        std::string realVersionDisplay = "版本" + version;
        ImGui::Text("Vacuum Music Player 3");
        ImGui::Text(realVersionDisplay.c_str());          // 你可以从资源或宏读取版本号
        ImGui::Text("作者: lazymonkey666");
        ImGui::Separator();

        // GitHub 仓库链接（可点击）
        if (ImGui::Button("作者链接")) {
            ShellExecuteA(NULL, "open", "https://github.com/lazymonkey666", NULL, NULL, SW_SHOWNORMAL);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::Separator();
    }

    void save() override {
        // 关于页面不需要保存任何配置，空实现
    }

    void load() override {
        // 无需加载配置
    }
};

// 设置分类
struct SettingCategory {
    std::string name;
    std::vector<std::unique_ptr<SettingItemBase>> items;
    SettingCategory(const char* n) : name(n) {}
};

class SettingWindow {
public:
    SettingWindow();
    void render();
    void setOpen(bool open) { open_ = open; }
    bool isOpen() const { return open_; }

private:
    ManageConfig& cfg;
    std::vector<SettingCategory> categories_;
    bool open_ = false;
    int selectedCategory_ = 0;
};