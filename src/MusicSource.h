#pragma once
#include <string>
#include <vector>
#include <memory>
#include <SettingItem.h>

// ==========================================
// 1. 基础结构（本地与在线公有的基础信息，UI 列表渲染专用）
// ==========================================
struct BaseTrackInfo {
    std::string id;          // 唯一标识：本地为路径，在线为网易云ID
    std::string displayName; // UI 列表显示的名字

    virtual ~BaseTrackInfo() = default;
};

// ==========================================
// 2. 统一播放准备信息（C函数的返回值，主播放器统一下载/播放的凭据）
// ==========================================
struct PlaybackInfo {
    bool isValid = false;
    bool isOnline = false;      // 是否为在线音源

    // === 播放器本体核心需要的 ===
    std::string audioUrl;       // 真实的音频地址（本地绝对路径 或 HTTP URL）
    std::string localCacheId;   // 用于生成缓存文件名（本地用原路径，在线用ID）

    // === 播放器统一下载封面需要的 ===
    std::string albumArtUrl;    // 封面下载地址（本地为空，在线为真实URL）

    // === 快速读取的元数据（可选，在线源直接提供可省去主程序读 TagLib 的时间）===
    std::string title;
    std::string artist;
    std::string album;
    bool needTags = false;      // ★ 新增：是否需要下载封面并写入标签
    std::string lyrics;         // ★ 新增：歌词内容
};

// ==========================================
// 3. 数据源接口（不变，仅在此确认签名）
// ==========================================
class IMusicSource {
public:
    virtual ~IMusicSource() = default;
    virtual bool Init() = 0;
    virtual size_t GetCount() const = 0;
    virtual const BaseTrackInfo* GetBaseTrack(size_t index) const = 0;
    virtual PlaybackInfo PreparePlayback(size_t index) = 0;
    virtual void AddSettings(std::vector<std::unique_ptr<SettingItemBase>>& items) {}
    virtual int GetLastIndex() const = 0;
    virtual void SaveIndex(int index) = 0;
};

// ==========================================
// 4. 工厂注册表（★ 新增 SourceDescriptor）
// ==========================================
struct SourceDescriptor {
    std::string id;            // "local" / "online"
    std::string displayName;   // "本地播放" / "在线（网易云）"
    std::function<std::unique_ptr<IMusicSource>()> creator;
};

class MusicSourceRegistry {
public:
    using CreatorFunc = std::function<std::unique_ptr<IMusicSource>()>;

    static MusicSourceRegistry& GetInstance() {
        static MusicSourceRegistry inst;
        return inst;
    }

    // 兼容旧接口（不建议再直接使用，走 Descriptor 版）
    void Register(const std::string& id, CreatorFunc creator) {
        descriptors_.push_back({ id, id, std::move(creator) });
    }

    // ★ 推荐接口：带显示名
    void Register(const SourceDescriptor& d) {
        descriptors_.push_back(d);
    }

    std::unique_ptr<IMusicSource> Create(const std::string& id) {
        for (auto& d : descriptors_) {
            if (d.id == id) return d.creator();
        }
        return nullptr;
    }

    const std::vector<SourceDescriptor>& GetDescriptors() const { return descriptors_; }

    std::vector<std::string> GetAllIds() const {
        std::vector<std::string> ids;
        for (auto& d : descriptors_) ids.push_back(d.id);
        return ids;
    }

private:
    MusicSourceRegistry() = default;
    std::vector<SourceDescriptor> descriptors_;
};

std::unique_ptr<IMusicSource> CreateLocalSource();
std::unique_ptr<IMusicSource> CreateNeteaseSource();