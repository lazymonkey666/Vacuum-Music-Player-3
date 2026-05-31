#include "SettingItem.h"
#include "manageconfig.h"
#include <string>
#include <windows.h>

// 全局配置变量（实际应放在配置管理类中）
static bool g_autoPlay = true;
static int g_volume = 70;
static bool g_showLyric = true;
static int g_theme = 0;
static bool g_minimizeToTray = false;
// 持久化的音乐文件夹路径，避免从临时返回值构造unique_ptr时模板推导失败
static std::string g_musicFolder;
static int g_acrylic_opacity=128;
static std::string g_amllAddress = "ws://localhost:11144/";
static std::string g_themeStyle = "System";

SettingWindow::SettingWindow()
    : cfg(ManageConfig::GetInstance())
{
    // Ensure config loaded
    ManageConfig::GetInstance().Load();

    g_musicFolder = ManageConfig::GetInstance().GetValue<std::string>("music_folder", "");
    g_acrylic_opacity = ManageConfig::GetInstance().GetValue<int>("acrylic_opacity", g_acrylic_opacity);
    g_amllAddress = ManageConfig::GetInstance().GetValue<std::string>("amll_address", g_amllAddress);

    SettingCategory general("通用");
    general.items.push_back(std::make_unique<SettingItem<std::string>>(g_musicFolder, "music_folder", "音乐文件夹"));
    categories_.push_back(std::move(general));

    // 歌词分类
    SettingCategory lyric("歌词");
    lyric.items.push_back(std::make_unique<SettingItemText>(g_amllAddress,"amll_address","AMLL 音乐播放器地址"));
    categories_.push_back(std::move(lyric));

    // 外观分类
    SettingCategory appearance("外观");
    appearance.items.push_back(std::make_unique<SettingItem<int>>(g_acrylic_opacity, "acrylic_opacity", "亚克力透明度", 1, 255));

    // 定义映射表：{显示中文, 实际保存值}
    std::vector<std::pair<std::string, std::string>> themeOptions = {
        {"深色", "Dark"},
        {"浅色", "Light"},
        {"跟随系统", "System"}
    };

    // 或者使用单选按钮版本
    appearance.items.push_back(std::make_unique<SettingItemRadio>(
        g_themeStyle,
        "theme_style",
        "主题风格",
        themeOptions
    ));
    categories_.push_back(std::move(appearance));

    // 关于分类
    SettingCategory about("关于");
    about.items.push_back(std::make_unique<SettingItemAbout>("about", "关于"));
    categories_.push_back(std::move(about));
}

void SettingWindow::render() {
    if (!open_) return;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(700, 230), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("设置", &open_, ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    // 左侧分类列表
    ImGui::BeginChild("Categories", ImVec2(150, 0), true);
    for (int i = 0; i < (int)categories_.size(); ++i) {
        if (ImGui::Selectable(categories_[i].name.c_str(), selectedCategory_ == i)) {
            selectedCategory_ = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 右侧设置项（带滚动条）
    ImGui::BeginChild("Settings", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), true);
    if (selectedCategory_ >= 0 && selectedCategory_ < (int)categories_.size()) {
        auto& cat = categories_[selectedCategory_];
        ImGui::Text("【%s】", cat.name.c_str());
        ImGui::Separator();
        for (auto& item : cat.items) {
            item->render();
            ImGui::Spacing();
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();

    // 在右下角绘制按钮
    float buttonWidth = 100.0f;
    float buttonHeight = ImGui::GetFrameHeight();
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 saveBtnPos(
        windowPos.x + windowSize.x - buttonWidth - spacing - 10,
        windowPos.y + windowSize.y - buttonHeight - 10
    );
    ImGui::SetCursorScreenPos(saveBtnPos);
    if (ImGui::Button("保存设置", ImVec2(buttonWidth, buttonHeight))) {
        for (auto& catAll : categories_) {
            for (auto& item : catAll.items) item->save();
        }
        // 保存并软重启程序以应用设置
        // Soft restart: save config, spawn new process with same command line, then exit
        auto SoftRestart = []() {
            ManageConfig::GetInstance().Save();
            LPWSTR cmdLine = GetCommandLineW();
            std::wstring cmd(cmdLine);
            STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
            PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
            if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                ExitProcess(0);
            }
        };
        SoftRestart();

    }


    ImGui::End();
}