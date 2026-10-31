#include "LinuxThemeDetector.h"
#include <iostream>
#include <cstring>

// XDG Desktop Portal 相关常量
static const char* PORTAL_BUS_NAME = "org.freedesktop.portal.Desktop";
static const char* PORTAL_OBJECT_PATH = "/org/freedesktop/portal/desktop";
static const char* PORTAL_INTERFACE = "org.freedesktop.portal.Settings";
static const char* APPEARANCE_NAMESPACE = "org.freedesktop.appearance";
static const char* COLOR_SCHEME_KEY = "color-scheme";

LinuxThemeDetector::LinuxThemeDetector() {
    DBusError error;
    dbus_error_init(&error);
    m_connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        std::cerr << "D-Bus Connection Error: " << error.message << std::endl;
        dbus_error_free(&error);
        m_connection = nullptr;
    }
}

LinuxThemeDetector::~LinuxThemeDetector() {
    StopListening();
    if (m_connection) {
        // 如果是通过 dbus_bus_get 获取的共享连接，通常不需要手动关闭，但最好释放资源
        // dbus_connection_unref(m_connection); // 视情况决定是否 unref
    }
}

SystemTheme LinuxThemeDetector::ParseColorScheme(DBusMessage* message) {
    DBusMessageIter iter, variant_iter;
    if (!dbus_message_iter_init(message, &iter)) return SystemTheme::Unknown;

    // 期望的返回类型是 Variant (v)，里面包含一个 Uint32 (u)
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) return SystemTheme::Unknown;

    dbus_message_iter_recurse(&iter, &variant_iter);
    if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_UINT32) return SystemTheme::Unknown;

    dbus_uint32_t scheme = 0;
    dbus_message_iter_get_basic(&variant_iter, &scheme);

    // 0: No preference, 1: Prefer Dark, 2: Prefer Light
    switch (scheme) {
    case 1: return SystemTheme::Dark;
    case 2: return SystemTheme::Light;
    case 0:
    default: return SystemTheme::Unknown;
    }
}

SystemTheme LinuxThemeDetector::GetCurrentTheme() {
    if (!m_connection) return SystemTheme::Unknown;

    DBusMessage* msg = dbus_message_new_method_call(
        PORTAL_BUS_NAME,
        PORTAL_OBJECT_PATH,
        PORTAL_INTERFACE,
        "Read"
    );
    if (!msg) return SystemTheme::Unknown;

    DBusMessageIter args;
    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &APPEARANCE_NAMESPACE);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &COLOR_SCHEME_KEY);

    DBusError error;
    dbus_error_init(&error);
    // 设置超时时间，避免桌面环境不支持时卡死（这里设置为 1000 毫秒）
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(m_connection, msg, 1000, &error);

    SystemTheme result = SystemTheme::Unknown;
    if (dbus_error_is_set(&error)) {
        std::cerr << "D-Bus Read Error: " << error.message << std::endl;
        dbus_error_free(&error);
    }
    else if (reply) {
        result = ParseColorScheme(reply);
        dbus_message_unref(reply);
    }

    dbus_message_unref(msg);
    return result;
}

void LinuxThemeDetector::SetThemeChangeCallback(std::function<void(SystemTheme)> callback) {
    m_callback = std::move(callback);
}

DBusHandlerResult LinuxThemeDetector::SignalHandler(DBusConnection* connection, DBusMessage* message, void* user_data) {
    auto* detector = static_cast<LinuxThemeDetector*>(user_data);

    if (dbus_message_is_signal(message, PORTAL_INTERFACE, "SettingChanged")) {
        DBusMessageIter iter, variant_iter;
        if (!dbus_message_iter_init(message, &iter)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char* namespace_str = nullptr;
        const char* key_str = nullptr;

        // 解析参数：SettingChanged(String namespace, String key, Variant value)
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&iter, &namespace_str);
            if (dbus_message_iter_next(&iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&iter, &key_str);
            }
        }

        // 只关心 appearance 的 color-scheme
        if (namespace_str && key_str &&
            strcmp(namespace_str, APPEARANCE_NAMESPACE) == 0 &&
            strcmp(key_str, COLOR_SCHEME_KEY) == 0) {

            if (dbus_message_iter_next(&iter)) { // 移动到 Variant 值
                dbus_message_iter_recurse(&iter, &variant_iter);
                if (dbus_message_iter_get_arg_type(&variant_iter) == DBUS_TYPE_UINT32) {
                    dbus_uint32_t scheme = 0;
                    dbus_message_iter_get_basic(&variant_iter, &scheme);

                    if (detector->m_callback) {
                        SystemTheme new_theme = (scheme == 1) ? SystemTheme::Dark :
                            (scheme == 2) ? SystemTheme::Light : SystemTheme::Unknown;
                        detector->m_callback(new_theme);
                    }
                }
            }
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void LinuxThemeDetector::StartListening() {
    if (m_isListening || !m_connection) return;

    // 注册信号匹配规则，告诉 D-Bus 我们想监听来自 Portal 的 SettingChanged 信号
    const char* match_rule = "type='signal',interface='org.freedesktop.portal.Settings',member='SettingChanged'";
    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(m_connection, match_rule, &error);
    if (dbus_error_is_set(&error)) {
        std::cerr << "Failed to add D-Bus match rule: " << error.message << std::endl;
        dbus_error_free(&error);
        return;
    }

    // 注册消息过滤器
    dbus_connection_add_filter(m_connection, SignalHandler, this, nullptr);
    m_isListening = true;
}

void LinuxThemeDetector::StopListening() {
    if (!m_isListening || !m_connection) return;

    dbus_connection_remove_filter(m_connection, SignalHandler, this);

    const char* match_rule = "type='signal',interface='org.freedesktop.portal.Settings',member='SettingChanged'";
    dbus_bus_remove_match(m_connection, match_rule, nullptr);

    m_isListening = false;
}