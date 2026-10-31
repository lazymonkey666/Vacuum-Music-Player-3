#include "MusicSource.h"
#include <platform/platform.h>
#include <manageconfig.h>
#include <filesystem>
#include <algorithm>

extern std::string PickFolderDialog(WindowHandle hwndOwner);
extern std::string u8string_to_string(const std::u8string& u8str);

// ═══════════════════════════════════════════════════════════
// ★ 本模块私有设置存储（不污染全局）
// ═══════════════════════════════════════════════════════════
static std::string s_musicFolder = "";
static bool        s_recursiveScan = false;

static void LoadLocalSettingsFromConfig() {
    auto& cfg = ManageConfig::GetInstance();
    s_musicFolder = cfg.GetValue<std::string>("music_folder", "");
    s_recursiveScan = cfg.GetValue<bool>("recursive_scan", false);
}

// 参数化递归，避免再回头读配置
std::vector<std::string> GetMusicFiles(const std::string& directory_utf8, bool recursive)
{
    struct FileInfo { std::string path; std::filesystem::file_time_type last_write; };
    std::vector<FileInfo> temp;

    PathType dirPath = Utf8ToPath(directory_utf8);
    if (dirPath.empty()) return {};

    auto hasMusicExt = [](const std::filesystem::path& p) {
        std::string ext = p.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return ext == ".mp3" || ext == ".flac" || ext == ".wav";
        };

    try {
        if (recursive) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(dirPath)) {
                if (!e.is_regular_file() || !hasMusicExt(e.path())) continue;
                std::string abs = u8string_to_string(e.path().u8string());
                std::replace(abs.begin(), abs.end(), '\\', '/');
                temp.push_back({ std::move(abs), e.last_write_time() });
            }
        }
        else {
            for (const auto& e : std::filesystem::directory_iterator(dirPath)) {
                if (!e.is_regular_file() || !hasMusicExt(e.path())) continue;
                std::string abs = u8string_to_string(e.path().u8string());
                std::replace(abs.begin(), abs.end(), '\\', '/');
                temp.push_back({ std::move(abs), e.last_write_time() });
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
    for (auto& i : temp) result.push_back(std::move(i.path));
    return result;
}

class LocalSource : public IMusicSource {
    std::vector<std::unique_ptr<BaseTrackInfo>> m_tracks;
    std::string m_rootPath;

public:
    bool Init() override {
        auto& cfg = ManageConfig::GetInstance();
        LoadLocalSettingsFromConfig();      // ★ 先同步 s_ 变量

        if (s_musicFolder.empty() || !std::filesystem::exists(Utf8ToPath(s_musicFolder))) {
            s_musicFolder = PickFolderDialog(g_hWnd);
            if (s_musicFolder.empty()) return false;
            cfg.SetValue("music_folder", s_musicFolder);
            cfg.Save();
        }
        m_rootPath = s_musicFolder;

        auto files = GetMusicFiles(m_rootPath, s_recursiveScan);
        for (const auto& f : files) {
            auto track = std::make_unique<BaseTrackInfo>();
            track->id = f;
            std::string rel = f.substr(m_rootPath.size());
            if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel.erase(0, 1);
            std::replace(rel.begin(), rel.end(), '\\', '/');
            track->displayName = rel;
            m_tracks.push_back(std::move(track));
        }
        return !m_tracks.empty();
    }

    size_t GetCount() const override { return m_tracks.size(); }
    const BaseTrackInfo* GetBaseTrack(size_t index) const override { return m_tracks[index].get(); }

    PlaybackInfo PreparePlayback(size_t index) override {
        PlaybackInfo info;
        if (index >= m_tracks.size()) return info;
        info.isValid = true;
        info.isOnline = false;
        info.audioUrl = m_tracks[index]->id;
        info.localCacheId = m_tracks[index]->id;
        return info;
    }

    // ═══════════════════════════════════════════════════════════
    // ★ 本源交出自己的设置项（引用 s_ 变量，天然双向绑定）
    // ═══════════════════════════════════════════════════════════
    void AddSettings(std::vector<std::unique_ptr<SettingItemBase>>& items) override {
        LoadLocalSettingsFromConfig();     // 从 config 刷新到 s_，防止脏值

        items.push_back(std::make_unique<SettingItem<std::string>>(
            s_musicFolder, "music_folder", "音乐文件夹"));
        items.push_back(std::make_unique<SettingItem<bool>>(
            s_recursiveScan, "recursive_scan", "递归查找文件夹内的所有歌曲"));
    }

    int GetLastIndex() const override {
        auto& cfg = ManageConfig::GetInstance();
        std::string last = cfg.GetValue<std::string>("last_music_local", "");
        if (last.empty()) return 0;
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i]->id == last) return (int)i;
        return 0;
    }

    void SaveIndex(int index) override {
        if (index < 0 || index >= (int)m_tracks.size()) return;
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue("last_music_local", m_tracks[index]->id);
        cfg.SetValue("last_music_online", "");
    }
};

std::unique_ptr<IMusicSource> CreateLocalSource() { return std::make_unique<LocalSource>(); }