#pragma once
#include <string>
#include <vector>
#include <memory>

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
// 3. 数据源接口
// ==========================================
class IMusicSource {
public:
    virtual ~IMusicSource() = default;

    // 初始化并加载列表（比如选文件夹、请求网易云API）
    virtual bool Init() = 0;

    // 列表总数
    virtual size_t GetCount() const = 0;

    // 返回公有的基础信息（UI 渲染用）
    virtual const BaseTrackInfo* GetBaseTrack(size_t index) const = 0;

    // C 函数：播放前准备（获取真实播放地址和所有元数据，包含在线私有数据）
    // 注意：如果是网络请求，此函数会阻塞，主程序必须放入异步线程！
    virtual PlaybackInfo PreparePlayback(size_t index) = 0;

    // 记录/恢复上次播放的索引
    virtual int GetLastIndex() const = 0;
    virtual void SaveIndex(int index) = 0;
};




// 工厂函数，在下方注册播放源
std::unique_ptr<IMusicSource> CreateLocalSource();
std::unique_ptr<IMusicSource> CreateNeteaseSource();