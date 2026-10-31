#include "MusicSource.h"
#include <platform/platform.h>
#include <manageconfig.h>
#include <NeteaseAPIManager.h>

extern std::string g_netease_user_cookie;

// ═══════════════════════════════════════════════════════════
// ★ 本模块私有设置存储
// ═══════════════════════════════════════════════════════════
static std::string s_apiAddress = "";
static std::string s_playlistId = "";
static bool        s_skipVip = false;

static void LoadNeteaseSettingsFromConfig() {
    auto& cfg = ManageConfig::GetInstance();
    s_apiAddress = cfg.GetValue<std::string>("netease_api_address", "");
    s_playlistId = cfg.GetValue<std::string>("netease_playlist_id", "");
    s_skipVip = cfg.GetValue<bool>("skip_vip", false);
}

struct NeteaseTrackInfo : public BaseTrackInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtUrl;
    int fee = 0;
};

class NeteaseSource : public IMusicSource {
    std::vector<std::unique_ptr<NeteaseTrackInfo>> m_tracks;

public:
    bool Init() override {
        auto& api = NeteaseAPIManager::GetInstance();
        LoadNeteaseSettingsFromConfig();

        if (s_apiAddress.empty()) return false;
        api.SetBaseURL(s_apiAddress);               // ★ 源自洽：自己保证 API 就绪

        if (s_playlistId.empty()) return false;

        try {
            auto ids = api.GetPlaylistTrackIds(std::stoll(s_playlistId));
            auto songs = api.GetSongsDetail(ids);
            for (auto& s : songs) {
                if (s_skipVip && !(s.fee == 0 || s.fee == 8)) continue;

                auto track = std::make_unique<NeteaseTrackInfo>();
                track->id = s.id;
                track->title = s.name;
                track->album = s.album;
                track->albumArtUrl = s.albumArtUrl;
                track->fee = s.fee;

                std::string artistsStr;
                for (size_t i = 0; i < s.artists.size(); ++i) {
                    if (i > 0) artistsStr += ", ";
                    artistsStr += s.artists[i];
                }
                track->artist = artistsStr;

                std::string prefix = (s.fee == 1 || s.fee == 2) ? "[VIP] " : "";
                track->displayName = prefix + s.name + " - " + artistsStr;
                m_tracks.push_back(std::move(track));
            }
            return !m_tracks.empty();
        }
        catch (...) { return false; }
    }

    size_t GetCount() const override { return m_tracks.size(); }
    const BaseTrackInfo* GetBaseTrack(size_t index) const override { return m_tracks[index].get(); }

    PlaybackInfo PreparePlayback(size_t index) override {
        PlaybackInfo info;
        if (index >= m_tracks.size()) return info;
        const auto& t = m_tracks[index];

        info.isOnline = true;
        info.localCacheId = t->id;
        info.title = t->title;
        info.artist = t->artist;
        info.album = t->album;
        info.needTags = true;
        info.lyrics = NeteaseAPIManager::GetInstance().GetLyrics(t->id);
        info.albumArtUrl = t->albumArtUrl;
        info.audioUrl = NeteaseAPIManager::GetInstance().GetSongUrl(
            t->id, "exhigh", g_netease_user_cookie);
        info.isValid = !info.audioUrl.empty();
        return info;
    }

    // ═══════════════════════════════════════════════════════════
    // ★ 本源交出自己的设置项
    // ═══════════════════════════════════════════════════════════
    void AddSettings(std::vector<std::unique_ptr<SettingItemBase>>& items) override {
        LoadNeteaseSettingsFromConfig();

        items.push_back(std::make_unique<SettingItemText>(
            s_apiAddress, "netease_api_address",
            "网易云API地址（BaseURL）\n例子：http://localhost:3000/\n注意：末尾要有斜杠“/”"));
        items.push_back(std::make_unique<SettingItemText>(
            s_playlistId, "netease_playlist_id",
            "网易云歌单ID\n例子：https://music.163.com/playlist?id=xxxxxxx\n其中的 xxxxxxx 部分填入"));
        items.push_back(std::make_unique<SettingItem<bool>>(
            s_skipVip, "skip_vip", "跳过需要 VIP 的音乐"));
    }

    int GetLastIndex() const override {
        auto& cfg = ManageConfig::GetInstance();
        std::string last = cfg.GetValue<std::string>("last_music_online", "");
        if (last.empty()) return 0;
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i]->id == last) return (int)i;
        return 0;
    }

    void SaveIndex(int index) override {
        if (index < 0 || index >= (int)m_tracks.size()) return;
        auto& cfg = ManageConfig::GetInstance();
        cfg.SetValue("last_music_online", m_tracks[index]->id);
        cfg.SetValue("last_music_local", "");
    }
};

std::unique_ptr<IMusicSource> CreateNeteaseSource() { return std::make_unique<NeteaseSource>(); }