#pragma once

#include <string>
#include <functional>
#include <memory>
#include <dbus/dbus.h>

// 定义深色/浅色模式枚举
enum class SystemTheme {
    Unknown, // 无法获取或没有偏好
    Light,   // 浅色模式
    Dark     // 深色模式
};

class LinuxThemeDetector {
public:
    LinuxThemeDetector();
    ~LinuxThemeDetector();

    // 禁止拷贝
    LinuxThemeDetector(const LinuxThemeDetector&) = delete;
    LinuxThemeDetector& operator=(const LinuxThemeDetector&) = delete;

    // 同步查询当前系统的主题偏好
    SystemTheme GetCurrentTheme();

    // 设置主题变化时的回调函数
    void SetThemeChangeCallback(std::function<void(SystemTheme)> callback);

    // 启动 D-Bus 事件监听（可以放在单独的线程中运行）
    void StartListening();

    // 停止监听
    void StopListening();

private:
    // 解析 D-Bus 返回的消息
    static SystemTheme ParseColorScheme(DBusMessage* message);

    // D-Bus 信号处理函数
    static DBusHandlerResult SignalHandler(DBusConnection* connection, DBusMessage* message, void* user_data);

    DBusConnection* m_connection = nullptr;
    bool m_isListening = false;
    std::function<void(SystemTheme)> m_callback;
};