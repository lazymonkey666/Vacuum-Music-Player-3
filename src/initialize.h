#pragma once
#include <string>
#include <platform/platform.h>

// 全局初始化管理器
namespace AppInit {

    // 核心初始化：读取配置、注册所有插件、启动事件总线、加载音乐列表
    bool InitCore();

    // 界面初始化：创建窗口、D3D、ImGui、热键、SMTC、加载主题
    bool InitUI(const std::string& className);

    // 切换播放后端（原 main.cpp 里的 SwitchPlaybackBackend）
    bool SwitchPlaybackBackend(const std::string& id);
}