#include "SettingItem.h"
#include "manageconfig.h"
#include <string>
#include <windows.h>

extern HANDLE g_hMutex;


// 全局配置变量（实际应放在配置管理类中）
static bool g_autoPlay = true;
static int g_volume = 70;
static bool g_showLyric = true;
static int g_theme = 0;
static bool g_minimizeToTray = false;
// 持久化的音乐文件夹路径，避免从临时返回值构造unique_ptr时模板推导失败
static std::string g_musicFolder;
static bool g_recursive_scan = false;
static int g_acrylic_opacity=128;
static std::string g_amllAddress = "ws://localhost:11144/";
static std::string g_themeStyle = "System";
static bool g_enableSnapToEdge = true;
static std::string g_playmode = "local";
static std::string g_neteaseapiaddress = "";
static std::string g_neteaseplaylistid = "";
static bool g_skipvip = false;
static bool g_enable_cache_limit = true;
static int g_cache_limit_mb = 2048;


SettingWindow::SettingWindow()
    : cfg(ManageConfig::GetInstance())
{
    // Ensure config loaded
    ManageConfig::GetInstance().Load();

    //g_musicFolder = ManageConfig::GetInstance().GetValue<std::string>("music_folder", "");
    //g_recursive_scan = ManageConfig::GetInstance().GetValue<bool>("recursive_scan", false);
    //g_acrylic_opacity = ManageConfig::GetInstance().GetValue<int>("acrylic_opacity", g_acrylic_opacity);
    //g_amllAddress = ManageConfig::GetInstance().GetValue<std::string>("amll_address", g_amllAddress);
    //g_enableSnapToEdge = ManageConfig::GetInstance().GetValue("enable_snap_to_edge", g_enableSnapToEdge);
    std::vector<std::pair<std::string, std::string>> PlayModeOptions = {
        {"本地播放", "local"},
        {"在线（需在下方配置网易云音乐API）", "online"},
    };


    SettingCategory general("通用");
    general.items.push_back(std::make_unique<SettingItem<std::string>>(g_musicFolder, "music_folder", "音乐文件夹"));
    general.items.push_back(std::make_unique<SettingItem<bool>>(g_recursive_scan, "recursive_scan", "递归查找文件夹内的所有歌曲"));
    general.items.push_back(std::make_unique<SettingItemRadio>(
        g_playmode,
        "play_mode",
        "播放来源",
        PlayModeOptions
    ));
    general.items.push_back(std::make_unique<SettingItemText>(g_neteaseapiaddress, "netease_api_address", "网易云API地址（BaseURL）\n例子：http://localhost:3000/\n注意：http协议和地址末尾要有斜杠“/”，否则程序可能不能正常运行"));
    general.items.push_back(std::make_unique<SettingItemText>(g_neteaseplaylistid, "netease_playlist_id", "网易云歌单ID\n例子：https://music.163.com/playlist?id=xxxxxxx&uct2=[某些字母]=\n其中的xxxxxxx部分填入选项"));
    general.items.push_back(std::make_unique<SettingItem<bool>>(g_skipvip, "skip_vip", "跳过需要的VIP音乐"));
    general.items.push_back(std::make_unique<SettingItem<bool>>(g_enable_cache_limit, "enable_cache_limit", "开启缓存限制"));
    general.items.push_back(std::make_unique<SettingItem<int>>(g_cache_limit_mb, "cache_limit_mb", "缓存大小限制(MB)", 512, 8192));
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

    // 使用单选按钮版本
    appearance.items.push_back(std::make_unique<SettingItem<bool>>(
        g_enableSnapToEdge,
        "enable_snap_to_edge",
        "启用窗口贴边停靠"
    ));
    appearance.items.push_back(std::make_unique<SettingItemRadio>(
        g_themeStyle,
        "theme_style",
        "主题风格",
        themeOptions
    ));

    //窗口是否贴边
    
    categories_.push_back(std::move(appearance));

    // 关于分类
    SettingCategory about("关于");
    about.items.push_back(std::make_unique<SettingItemAbout>("about", "关于"));
    categories_.push_back(std::move(about));

    for (auto& catAll : categories_) {
        for (auto& item : catAll.items) item->load();
    }
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
        // 保存所有设置项
        for (auto& catAll : categories_) {
            for (auto& item : catAll.items) item->save();
        }
        ManageConfig::GetInstance().Save();

        // 关闭互斥体，释放锁（让新进程可以创建自己的互斥体）
        if (g_hMutex) {
            CloseHandle(g_hMutex);
            g_hMutex = NULL;
        }

        // 启动新进程（使用当前命令行）
        LPWSTR cmdLine = GetCommandLineW();
        std::wstring cmd(cmdLine);
        STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
        if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        // 无论启动是否成功，当前进程退出
        ExitProcess(0);
    }


    ImGui::End();
}