#include "MusicSource.h"
#include <platform/platform.h>
#include <manageconfig.h>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <mutex>
#include <shared_mutex>  // ★ 引入读写锁
#include <set>
#include <unordered_set>
#include <atomic>
#include <string>
#include <vector>

#include <taglib/fileref.h>
#include <taglib/tag.h>

extern std::string PickFolderDialog(WindowHandle hwndOwner);
extern std::string u8string_to_string(const std::u8string& u8str);

static std::string s_musicFolder = "";
static bool        s_recursiveScan = false;

static void LoadLocalSettingsFromConfig() {
    auto& cfg = ManageConfig::GetInstance();
    s_musicFolder = cfg.GetValue<std::string>("music_folder", "");
    s_recursiveScan = cfg.GetValue<bool>("recursive_scan", false);
}

struct LocalTrackInfo : public BaseTrackInfo {
    bool isDetailLoaded = false;
    std::string filePath;
};

class LocalSource : public IMusicSource {
    std::vector<std::unique_ptr<LocalTrackInfo>> m_tracks;
    std::string m_rootPath;

    // ★ 核心：读写锁保护 m_tracks。后台写(unique_lock)，主线程读(shared_lock)
    mutable std::shared_mutex m_trackMtx;
    // 用于查重，O(1) 性能，防止 10万首扫描时死循环
    std::unordered_set<std::string> m_pathSet;

    // 懒加载专用锁
    std::mutex m_loadingMtx;
    std::set<size_t> m_loadingIndices;

    std::atomic<bool> m_isScanning{ false };

    // ★ 安全且极速的单条添加函数
    void AddSingleTrack(const std::string& f) {
        std::unique_lock lock(m_trackMtx); // 加上写锁
        if (m_pathSet.count(f)) return;    // 已存在，跳过
        m_pathSet.insert(f);

        auto track = std::make_unique<LocalTrackInfo>();
        track->id = f;
        track->filePath = f;
        track->isDetailLoaded = false;

        std::string rel = f.substr(m_rootPath.size());
        if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel.erase(0, 1);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        track->displayName = rel;

        m_tracks.push_back(std::move(track));
    }

    // ★ 通用扫描逻辑（供 Init 和 Sync 复用）
        // ★ 通用扫描逻辑（供 Init 和 Sync 复用）
    void BackgroundScanAndAdd() {
        if (m_isScanning.exchange(true)) return; // 已经在扫了

        std::error_code ec;
        PathType rootPathW = Utf8ToPath(m_rootPath);

        auto hasMusicExt = [](const std::filesystem::path& p) {
            std::string ext = p.extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            return ext == ".mp3" || ext == ".flac" || ext == ".wav" || ext == ".m4a";
            };

        struct FileInfo {
            std::string path;
            std::filesystem::file_time_type last_write;
        };
        std::vector<FileInfo> tempFiles;

        // 1. 先在后台把文件全部扫出来（不读标签，极快）
        try {
            if (s_recursiveScan) {
                for (const auto& e : std::filesystem::recursive_directory_iterator(rootPathW, ec)) {
                    if (!e.is_regular_file() || !hasMusicExt(e.path())) continue;
                    std::string abs = u8string_to_string(e.path().u8string());
                    std::replace(abs.begin(), abs.end(), '\\', '/');
                    tempFiles.push_back({ std::move(abs), e.last_write_time() });
                }
            }
            else {
                for (const auto& e : std::filesystem::directory_iterator(rootPathW, ec)) {
                    if (!e.is_regular_file() || !hasMusicExt(e.path())) continue;
                    std::string abs = u8string_to_string(e.path().u8string());
                    std::replace(abs.begin(), abs.end(), '\\', '/');
                    tempFiles.push_back({ std::move(abs), e.last_write_time() });
                }
            }
        }
        catch (...) {}

        // 2. 按照文件的最后修改时间降序排序（最新的在最上面）
        std::sort(tempFiles.begin(), tempFiles.end(),
            [](const FileInfo& a, const FileInfo& b) { return a.last_write > b.last_write; });

        // 3. 分批塞入主列表（每 1000 首加一次锁，让 UI 产生“不断添加”的视觉体验）
        const size_t batchSize = 1000;
        for (size_t i = 0; i < tempFiles.size(); i += batchSize) {
            {
                std::unique_lock lock(m_trackMtx); // 加上写锁
                for (size_t j = i; j < i + batchSize && j < tempFiles.size(); ++j) {
                    const auto& f = tempFiles[j].path;
                    if (m_pathSet.count(f)) continue; // 查重
                    m_pathSet.insert(f);

                    auto track = std::make_unique<LocalTrackInfo>();
                    track->id = f;
                    track->filePath = f;
                    track->isDetailLoaded = false;

                    std::string rel = f.substr(m_rootPath.size());
                    if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel.erase(0, 1);
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    track->displayName = rel;

                    m_tracks.push_back(std::move(track));
                }
            } // 锁在这里释放

            // 给主线程留出时间渲染（你就看到列表一截一截地长出来）
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        m_isScanning = false;
    }

public:
    virtual ~LocalSource() noexcept = default;

    bool Init() override {
        auto& cfg = ManageConfig::GetInstance();
        LoadLocalSettingsFromConfig();

        if (s_musicFolder.empty() || !std::filesystem::exists(Utf8ToPath(s_musicFolder))) {
            s_musicFolder = PickFolderDialog(g_hWnd);
            if (s_musicFolder.empty()) return false;
            cfg.SetValue("music_folder", s_musicFolder);
            cfg.Save();
        }
        m_rootPath = s_musicFolder;

        // ★ 极速小技巧：先检查上次播放的是哪首，如果还在，优先把它塞进列表
        // 这样主程序瞬间就能定位到上次播放的歌，不会卡在 0 索引
        std::string last = cfg.GetValue<std::string>("last_music_local", "");
        if (!last.empty() && std::filesystem::exists(Utf8ToPath(last))) {
            AddSingleTrack(last);
        }

        // ★ 开启后台疯狂扫描，主线程瞬间返回 true
        std::thread([this]() { BackgroundScanAndAdd(); }).detach();

        return true; // ★ 0 毫秒返回，瞬间启动！
    }

    // ★ 实现同步接口：每次调用都去检查有没有新增的歌
    void SyncPlaylistAsync() override {
        if (m_rootPath.empty()) return;
        std::thread([this]() { BackgroundScanAndAdd(); }).detach();
    }

    void RequestTrackDetails(size_t startIndex, size_t endIndex) override {
        std::vector<size_t> indicesToLoad;
        {
            std::lock_guard<std::mutex> loadLock(m_loadingMtx);
            std::shared_lock trackLock(m_trackMtx); // ★ 加上读锁
            for (size_t i = startIndex; i < endIndex && i < m_tracks.size(); ++i) {
                if (!m_tracks[i]->isDetailLoaded && m_loadingIndices.find(i) == m_loadingIndices.end()) {
                    indicesToLoad.push_back(i);
                    m_loadingIndices.insert(i);
                }
            }
        }

        if (indicesToLoad.empty()) return;

        std::thread([this, indicesToLoad]() {
            for (size_t idx : indicesToLoad) {
                std::string filePath;
                {
                    std::shared_lock trackLock(m_trackMtx); // ★ 读锁
                    if (idx >= m_tracks.size()) continue;
                    filePath = m_tracks[idx]->filePath;
                }

                std::string displayName = "";
                PathType path = Utf8ToPath(filePath);
                if (!path.empty()) {
                    try {
                        TagLib::FileRef f(path.c_str());
                        if (!f.isNull() && f.tag()) {
                            std::string title = f.tag()->title().to8Bit(true);
                            std::string artist = f.tag()->artist().to8Bit(true);
                            if (!title.empty()) {
                                displayName = title;
                                if (!artist.empty()) displayName += " - " + artist;
                            }
                        }
                    }
                    catch (...) {}
                }

                {
                    std::lock_guard<std::mutex> loadLock(m_loadingMtx);
                    std::unique_lock trackLock(m_trackMtx); // ★ 写锁
                    if (idx < m_tracks.size()) {
                        if (!displayName.empty()) m_tracks[idx]->displayName = displayName;
                        m_tracks[idx]->isDetailLoaded = true;
                    }
                    m_loadingIndices.erase(idx);
                }
            }
            }).detach();
    }

    // ★ 所有读 m_tracks 的方法，统统加上共享锁
    size_t GetCount() const override {
        std::shared_lock lock(m_trackMtx);
        return m_tracks.size();
    }

    const BaseTrackInfo* GetBaseTrack(size_t index) const override {
        std::shared_lock lock(m_trackMtx);
        if (index < m_tracks.size()) return m_tracks[index].get();
        return nullptr;
    }

    int FindTrackIndex(const std::string& id) const override {
        std::shared_lock lock(m_trackMtx);
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i]->id == id) return (int)i;
        }
        return -1;
    }

    int AddTrack(const std::string& filePath) override {
        std::unique_lock lock(m_trackMtx);
        if (m_pathSet.count(filePath)) {
            for (size_t i = 0; i < m_tracks.size(); ++i) {
                if (m_tracks[i]->id == filePath) return (int)i;
            }
        }
        m_pathSet.insert(filePath);

        auto track = std::make_unique<LocalTrackInfo>();
        track->id = filePath;
        track->filePath = filePath;
        track->isDetailLoaded = false;
        std::string rel = filePath.substr(m_rootPath.size());
        if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel.erase(0, 1);
        track->displayName = rel;
        m_tracks.push_back(std::move(track));
        return (int)m_tracks.size() - 1;
    }

    PlaybackInfo PreparePlayback(size_t index) override {
        PlaybackInfo info;
        std::shared_lock lock(m_trackMtx);
        if (index >= m_tracks.size()) return info;
        info.isValid = true;
        info.isOnline = false;
        info.audioUrl = m_tracks[index]->filePath;
        info.localCacheId = m_tracks[index]->filePath;
        return info;
    }

    int GetLastIndex() const override {
        auto& cfg = ManageConfig::GetInstance();
        std::string last = cfg.GetValue<std::string>("last_music_local", "");
        if (last.empty()) return 0;
        std::shared_lock lock(m_trackMtx);
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i]->filePath == last) return (int)i;
        }
        return 0;
    }

    void SaveIndex(int index) override {
        std::shared_lock lock(m_trackMtx);
        if (index < 0 || index >= (int)m_tracks.size()) return;
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue("last_music_local", m_tracks[index]->filePath);
        cfg.SetValue("last_music_online", "");
    }

    void AddSettings(std::vector<std::unique_ptr<SettingItemBase>>& items) override {
        LoadLocalSettingsFromConfig();
        items.push_back(std::make_unique<SettingItem<std::string>>(s_musicFolder, "music_folder", "音乐文件夹"));
        items.push_back(std::make_unique<SettingItem<bool>>(s_recursiveScan, "recursive_scan", "递归查找文件夹内的所有歌曲"));
    }
};

std::unique_ptr<IMusicSource> CreateLocalSource() { return std::make_unique<LocalSource>(); }