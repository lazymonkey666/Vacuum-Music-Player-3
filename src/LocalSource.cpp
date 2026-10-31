#include "MusicSource.h"
#include <platform/platform.h>
#include <manageconfig.h>
#include <filesystem>
#include <algorithm>

// 复用你原来 main.cpp 里的函数
extern std::string PickFolderDialog(WindowHandle hwndOwner);
extern std::string u8string_to_string(const std::u8string& u8str);

std::vector<std::string> GetMusicFiles(const std::string& directory_utf8) {
    struct FileInfo {
        std::string path;
        std::filesystem::file_time_type last_write;
    };
    std::vector<FileInfo> temp;

    PathType dirPath = Utf8ToPath(directory_utf8);
    if (dirPath.empty()) return {};

    // 判断扩展名（跨平台）
    auto hasMusicExt = [](const std::filesystem::path& p) {
        std::string ext = p.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return ext == ".mp3" || ext == ".flac" || ext == ".wav";
        };

    try {
        bool recursive = false;
        try {
            recursive = ManageConfig::GetInstance().GetValue<bool>("recursive_scan", false);
        }
        catch (...) { recursive = false; }

        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath)) {
                if (!entry.is_regular_file()) continue;
                if (!hasMusicExt(entry.path())) continue;

                std::string absPath = u8string_to_string(entry.path().u8string());
                std::replace(absPath.begin(), absPath.end(), '\\', '/');
                temp.emplace_back(FileInfo{ std::move(absPath), entry.last_write_time() });
            }
        }
        else {
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (!entry.is_regular_file()) continue;
                if (!hasMusicExt(entry.path())) continue;

                std::string absPath = u8string_to_string(entry.path().u8string());
                std::replace(absPath.begin(), absPath.end(), '\\', '/');
                temp.emplace_back(FileInfo{ std::move(absPath), entry.last_write_time() });
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        OutputDebugStringA(("文件系统错误: " + std::string(e.what()) + "\n").c_str());
    }

    std::sort(temp.begin(), temp.end(),
        [](const FileInfo& a, const FileInfo& b) { return a.last_write > b.last_write; });

    std::vector<std::string> result;
    result.reserve(temp.size());
    for (auto& info : temp) result.push_back(std::move(info.path));
    return result;
}

class LocalSource : public IMusicSource {
    std::vector<std::unique_ptr<BaseTrackInfo>> m_tracks;
    std::string m_rootPath;

public:
    bool Init() override {
        auto& cfg = ManageConfig::GetInstance();
        m_rootPath = cfg.GetValue<std::string>("music_folder", "");

        // 校验路径有效性
        if (m_rootPath.empty() || !std::filesystem::exists(Utf8ToPath(m_rootPath))) {
            m_rootPath = PickFolderDialog(g_hWnd);
            if (m_rootPath.empty()) return false;
            cfg.SetValue("music_folder", m_rootPath);
            cfg.Save();
        }

        auto files = GetMusicFiles(m_rootPath);
        for (const auto& f : files) {
            auto track = std::make_unique<BaseTrackInfo>();
            track->id = f;

            // 计算相对路径用于 UI 显示
            std::string relPath = f.substr(m_rootPath.size());
            if (!relPath.empty() && (relPath[0] == '/' || relPath[0] == '\\')) relPath.erase(0, 1);
            std::replace(relPath.begin(), relPath.end(), '\\', '/');
            track->displayName = relPath;

            m_tracks.push_back(std::move(track));
        }
        return !m_tracks.empty();
    }

    size_t GetCount() const override { return m_tracks.size(); }

    const BaseTrackInfo* GetBaseTrack(size_t index) const override {
        return m_tracks[index].get();
    }

    PlaybackInfo PreparePlayback(size_t index) override {
        PlaybackInfo info;
        if (index >= m_tracks.size()) return info;

        info.isValid = true;
        info.isOnline = false;
        info.audioUrl = m_tracks[index]->id;          // 本地直接返回绝对路径
        info.localCacheId = m_tracks[index]->id;      // 使用原路径作为标识
        info.albumArtUrl = "";                        // 本地没有网络封面

        // 本地的 title 等元数据留空，主播放器播的时候用 TagLib 自己去读
        return info;
    }

    int GetLastIndex() const override {
        auto& cfg = ManageConfig::GetInstance();
        std::string last = cfg.GetValue<std::string>("last_music_local", "");
        if (last.empty()) return 0;
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i]->id == last) return (int)i;
        }
        return 0;
    }

    void SaveIndex(int index) override {
        if (index < 0 || index >= (int)m_tracks.size()) return;
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue("last_music_local", m_tracks[index]->id);
        cfg.SetValue("last_music_online", ""); // 清空另一种模式的记录
    }
};

std::unique_ptr<IMusicSource> CreateLocalSource() { return std::make_unique<LocalSource>(); }