#pragma once
#include <platform/platform.h>
#include <SettingItem.h>

class Application {
public:
    static Application& Get();
    int  Run(int argc, char** argv);
    void RequestQuit() { m_quit = true; }
    WindowHandle  Hwnd()     const { return m_hwnd; }
    SettingWindow& Settings() { return m_settings; }

private:
    Application() = default;
    bool InitConfig();
    bool InitUI();
    int  MainLoop();
    bool PlatformInitWindow(const std::string& className);
    bool PlatformReadyToRender();

    WindowHandle  m_hwnd = nullptr;
    SettingWindow m_settings;
    int  m_counter = 0;
    bool m_quit = false;
};
