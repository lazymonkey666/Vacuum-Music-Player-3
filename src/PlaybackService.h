#pragma once
#include <platform/platform.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════
// 服务接口
// ═══════════════════════════════════════════════════════════
class IPlaybackService {
public:
    virtual ~IPlaybackService() = default;

    virtual const char* Id() const = 0;
    virtual const char* DisplayName() const = 0;

    // Init 里做 Subscribe / 建连接；Shutdown 里做 Unsubscribe / 断连
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    // 需要周期回调的服务（如 AMLL）覆盖；简单服务（如 SMTC）不用管
    virtual void Poll(uint64_t /*currentMs*/, bool /*isPlaying*/) {}
    
    virtual bool HasWindow() const { return false; }
    virtual void ShowWindow() {}
    virtual void RenderWindow() {}
};

// ═══════════════════════════════════════════════════════════
// 描述符
// ═══════════════════════════════════════════════════════════
struct PlaybackServiceDescriptor {
    std::string id;
    std::string displayName;
    std::string configKey;        // 空字符串 = 不持久化
    bool        defaultEnabled;   // 首次启动时的默认值（userConfigurable=false 时忽略）
    std::function<std::unique_ptr<IPlaybackService>()> creator;
    bool        userConfigurable = true;   // 静默服务，不可关闭、不显示
};

// ═══════════════════════════════════════════════════════════
// 注册表
// ═══════════════════════════════════════════════════════════
class PlaybackServiceRegistry {
public:
    static PlaybackServiceRegistry& GetInstance();

    // ── 注册（启动时调用一次）──
    void Register(const PlaybackServiceDescriptor& d);
    const std::vector<PlaybackServiceDescriptor>& GetDescriptors() const;

    // ── 启用/停用 ──
    bool Enable(const std::string& id);
    void Disable(const std::string& id);
    bool IsEnabled(const std::string& id) const;

    // ── 按配置启用（启动时调用一次）──
    void EnableDefaults();

    // ── 主循环每秒调用一次 ──
    void Tick(uint64_t currentMs, bool isPlaying);

    // ── 退出时清理 ──
    void ShutdownAll();

    // ★ 按 id 拿服务实例（供 Backend 访问）
    IPlaybackService* GetService(const std::string& id);

    // ★ 主循环每帧调一次：渲染所有有窗口的服务
    void RenderAllWindows();

private:
    PlaybackServiceRegistry() = default;

    mutable std::mutex mtx_;
    std::vector<PlaybackServiceDescriptor> descriptors_;
    // ★ shared_ptr：允许 Tick 快照持有，即使 Disable 了也不悬空
    std::map<std::string, std::shared_ptr<IPlaybackService>> instances_;

    
};

// ═══════════════════════════════════════════════════════════
// 工厂
// ═══════════════════════════════════════════════════════════
std::unique_ptr<IPlaybackService> CreateSmtcService();
std::unique_ptr<IPlaybackService> CreateAMLLService();