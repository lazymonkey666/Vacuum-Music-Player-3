#pragma once

// ═══════════════════════════════════════════════════════════
// platform.h 必须最先 include
// ═══════════════════════════════════════════════════════════
#include <platform/platform.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════
// 诊断（编译失败时一眼定位）
// ═══════════════════════════════════════════════════════════
static_assert(sizeof(PathType) >= sizeof(std::string),
    "PathType 未定义或类型异常，请检查 platform.h");
static_assert(sizeof(std::uint64_t) == 8, "uint64_t 不可用");

// ═══════════════════════════════════════════════════════════
// 设备信息
// ═══════════════════════════════════════════════════════════
struct PlaybackDeviceInfo {
    std::string id;
    std::string displayName;
    std::string location;
};

// ═══════════════════════════════════════════════════════════
// 后端接口
// ═══════════════════════════════════════════════════════════
class IPlaybackBackend {
public:
    virtual ~IPlaybackBackend() = default;

    virtual const char* Id() const = 0;
    virtual const char* DisplayName() const = 0;

    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    virtual std::uint64_t RequestLoad(const PathType& widePath, std::uint64_t userTag = 0) = 0;
    virtual std::uint64_t RequestUnload(std::uint64_t userTag = 0) = 0;
    virtual std::uint64_t RequestPlay(std::uint64_t userTag = 0) = 0;
    virtual std::uint64_t RequestPause(std::uint64_t userTag = 0) = 0;
    virtual std::uint64_t RequestStop(std::uint64_t userTag = 0) = 0;
    virtual std::uint64_t RequestSeek(std::uint64_t ms, std::uint64_t userTag = 0) = 0;

    virtual bool          IsLoaded()      const = 0;
    virtual bool          IsPlaying()     const = 0;
    virtual bool          IsAtEnd()       const = 0;
    virtual std::uint64_t GetPositionMs() const = 0;
    virtual std::uint64_t GetDurationMs() const = 0;

    virtual bool SupportsDeviceManagement() const { return false; }
    virtual void StartDeviceSearch() {}
    virtual bool IsSearchingDevices() const { return false; }
    virtual std::vector<PlaybackDeviceInfo> PollFoundDevices() { return {}; }
    virtual bool ConnectToDevice(const std::string& /*location*/) { return false; }
    virtual bool IsDeviceConnected() const { return true; }
};

// ═══════════════════════════════════════════════════════════
// 注册表
// ═══════════════════════════════════════════════════════════
struct PlaybackBackendDescriptor {
    std::string id;
    std::string displayName;
    std::function<std::unique_ptr<IPlaybackBackend>()> creator;
};

class PlaybackBackendRegistry {
public:
    static PlaybackBackendRegistry& GetInstance() {
        static PlaybackBackendRegistry inst;
        return inst;
    }

    void Register(const PlaybackBackendDescriptor& d) {
        std::lock_guard<std::mutex> lock(mtx_);
        descriptors_.push_back(d);
    }

    const std::vector<PlaybackBackendDescriptor>& GetDescriptors() const {
        return descriptors_;
    }

    IPlaybackBackend* GetOrCreate(const std::string& id) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = instances_.find(id);
        if (it != instances_.end()) return it->second.get();

        for (auto& d : descriptors_) {
            if (d.id == id) {
                auto inst = d.creator();
                if (!inst) return nullptr;
                IPlaybackBackend* raw = inst.get();
                instances_[id] = std::move(inst);
                return raw;
            }
        }
        return nullptr;
    }

    bool Activate(const std::string& id) {
        auto* next = GetOrCreate(id);
        if (!next) return false;
        if (!next->Init()) return false;

        std::lock_guard<std::mutex> lock(mtx_);
        if (active_ && active_ != next) {
            try { active_->Shutdown(); }
            catch (...) {}
        }
        active_ = next;
        activeId_ = id;
        return true;
    }

    IPlaybackBackend* Active() {
        std::lock_guard<std::mutex> lock(mtx_);
        return active_;
    }

    std::string ActiveId() {
        std::lock_guard<std::mutex> lock(mtx_);
        return activeId_;
    }

    void ShutdownAll() {
        std::map<std::string, std::unique_ptr<IPlaybackBackend>> toKill;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            active_ = nullptr;
            activeId_.clear();
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

private:
    PlaybackBackendRegistry() = default;

    mutable std::mutex mtx_;
    std::vector<PlaybackBackendDescriptor> descriptors_;
    std::map<std::string, std::unique_ptr<IPlaybackBackend>> instances_;
    IPlaybackBackend* active_ = nullptr;
    std::string activeId_;
};

// ═══════════════════════════════════════════════════════════
// 工厂
// ═══════════════════════════════════════════════════════════
std::unique_ptr<IPlaybackBackend> CreateMiniaudioBackend();
std::unique_ptr<IPlaybackBackend> CreateSonosBackend();