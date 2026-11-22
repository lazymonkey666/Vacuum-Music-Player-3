#pragma once
#include <string>
#include <platform/platform.h>

// 全局初始化管理器
namespace AppInit {
    bool InitCore();
    bool InitUI(const std::string& className);
    bool SwitchPlaybackBackend(const std::string& id);
    void ConfigureStateMachine();

    bool InitWindowPlatform(const std::string& className);   // 加
    void RegisterMusicSources();                              // 加
    void RegisterPlaybackSystems();                           // 加
    void RegisterServices();                                  // 加
    void ReadConfig();
    void LoadTheme();
}
