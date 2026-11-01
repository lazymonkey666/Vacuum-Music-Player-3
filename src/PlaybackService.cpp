#include "PlaybackService.h"
#include <ManageConfig.h>
#include <platform/platform.h>

// ═══════════════════════════════════════════════════════════
PlaybackServiceRegistry& PlaybackServiceRegistry::GetInstance() {
    static PlaybackServiceRegistry inst;
    return inst;
}

// ─────────────────────────────────────────────────────────
void PlaybackServiceRegistry::Register(const PlaybackServiceDescriptor& d) {
    std::lock_guard<std::mutex> lock(mtx_);
    descriptors_.push_back(d);
}

const std::vector<PlaybackServiceDescriptor>&
PlaybackServiceRegistry::GetDescriptors() const {
    return descriptors_;    // 只读访问，调用方不要在持有时修改
}

// ─────────────────────────────────────────────────────────
bool PlaybackServiceRegistry::Enable(const std::string& id) {
    std::shared_ptr<IPlaybackService> svc;
    std::string configKey;

    {
        std::lock_guard<std::mutex> lock(mtx_);

        // 已启用直接返回
        if (instances_.count(id)) return true;

        // 找到描述符
        const PlaybackServiceDescriptor* desc = nullptr;
        for (auto& d : descriptors_) {
            if (d.id == id) { desc = &d; break; }
        }
        if (!desc) return false;

        auto inst = desc->creator();
        if (!inst) return false;

        // 注意：Init 可能耗时（AMLL 建连接），但持锁期间 Init 会拖住其他线程
        //   → 把 Init 挪到锁外
        svc = std::shared_ptr<IPlaybackService>(std::move(inst));
        configKey = desc->configKey;
    }

    // 锁外 Init
    bool ok = false;
    try { ok = svc->Init(); }
    catch (const std::exception& e) {
        OutputDebugStringA(("[Service Enable] " + id + " Init exception: " + e.what() + "\n").c_str());
        return false;
    }
    catch (...) {
        OutputDebugStringA(("[Service Enable] " + id + " Init unknown exception\n").c_str());
        return false;
    }
    if (!ok) return false;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        // 再检查一次（可能有并发 Enable 同一 id）
        if (instances_.count(id)) {
            svc->Shutdown();    // 多余的那个关掉
            return true;
        }
        instances_[id] = svc;
    }

    // 写配置（锁外，避免 cfg 内部可能加锁）
    if (!configKey.empty()) {
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue(configKey, true);
        cfg.Save();
    }
    return true;
}

// ─────────────────────────────────────────────────────────
void PlaybackServiceRegistry::Disable(const std::string& id) {
    std::shared_ptr<IPlaybackService> svc;
    std::string configKey;

    {
        std::lock_guard<std::mutex> lock(mtx_);

        // ★ 先找描述符并检查可配置性
        const PlaybackServiceDescriptor* desc = nullptr;
        for (auto& d : descriptors_) {
            if (d.id == id) { desc = &d; break; }
        }
        if (!desc) return;

        if (!desc->userConfigurable) {
            OutputDebugStringA(("[Service] " + id +
                " is not configurable, ignore Disable\n").c_str());
            return;   // ★ 静默服务不可关
        }

        auto it = instances_.find(id);
        if (it == instances_.end()) return;

        svc = it->second;
        instances_.erase(it);
        configKey = desc->configKey;
    }

    if (svc) {
        try { svc->Shutdown(); }
        catch (...) {}
    }

    if (!configKey.empty()) {
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue(configKey, false);
        cfg.Save();
    }
}

// ─────────────────────────────────────────────────────────
bool PlaybackServiceRegistry::IsEnabled(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    return instances_.count(id) > 0;
}

// ─────────────────────────────────────────────────────────
void PlaybackServiceRegistry::EnableDefaults() {
    std::vector<std::string> toEnable;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto& cfg = ManageConfig::GetInstance();
        for (auto& d : descriptors_) {
            bool shouldEnable;
            if (!d.userConfigurable) {
                // ★ 静默服务：永远启用，不看配置
                shouldEnable = true;
            }
            else {
                shouldEnable = d.defaultEnabled;
                if (!d.configKey.empty()) {
                    shouldEnable = cfg.GetValue<bool>(d.configKey, d.defaultEnabled);
                }
            }
            if (shouldEnable) toEnable.push_back(d.id);
        }
    }

    for (const auto& id : toEnable) {
        Enable(id);
    }
}

// ─────────────────────────────────────────────────────────
void PlaybackServiceRegistry::Tick(uint64_t currentMs, bool isPlaying) {
    // 快照：锁内拷贝 shared_ptr，锁外调用 Poll
    std::vector<std::shared_ptr<IPlaybackService>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        snapshot.reserve(instances_.size());
        for (auto& kv : instances_) {
            snapshot.push_back(kv.second);    // shared_ptr 拷贝，保证不悬空
        }
    }

    for (auto& svc : snapshot) {
        if (!svc) continue;
        try {
            svc->Poll(currentMs, isPlaying);
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("[Service Poll] " + std::string(svc->Id())
                + " exception: " + e.what() + "\n").c_str());
        }
        catch (...) {
            OutputDebugStringA(("[Service Poll] " + std::string(svc->Id())
                + " unknown exception\n").c_str());
        }
    }
}

// ─────────────────────────────────────────────────────────
void PlaybackServiceRegistry::ShutdownAll() {
    std::map<std::string, std::shared_ptr<IPlaybackService>> toKill;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        toKill = std::move(instances_);
        instances_.clear();
    }

    for (auto& kv : toKill) {
        if (kv.second) {
            try { kv.second->Shutdown(); }
            catch (...) {}
        }
    }
}