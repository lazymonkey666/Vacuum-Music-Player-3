#include "MusicSource.h"
#include <platform/platform.h>
#include <manageconfig.h>
#include <NeteaseAPIManager.h>

extern std::string g_netease_user_cookie;

// ★ 网易云专属的私有结构
struct NeteaseTrackInfo : public BaseTrackInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtUrl; // 私有专属封面
    int fee = 0;             // 私有专属付费标记
};

class NeteaseSource : public IMusicSource {
    std::vector<std::unique_ptr<NeteaseTrackInfo>> m_tracks;

public:
    bool Init() override {
        auto& cfg = ManageConfig::GetInstance();
        auto& api = NeteaseAPIManager::GetInstance();

        try {
            std::string playlistID = cfg.GetValue<std::string>("netease_playlist_id", "");
            if (playlistID.empty()) return false;

            auto ids = api.GetPlaylistTrackIds(std::stoll(playlistID));
            auto songs = api.GetSongsDetail(ids);
            bool skipVip = cfg.GetValue<bool>("skip_vip", false);

            for (auto& s : songs) {
                if (skipVip && !(s.fee == 0 || s.fee == 8)) continue;

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
        catch (...) {
            return false;
        }
    }

    size_t GetCount() const override { return m_tracks.size(); }
    const BaseTrackInfo* GetBaseTrack(size_t index) const override { return m_tracks[index].get(); }

    PlaybackInfo PreparePlayback(size_t index) override {
        PlaybackInfo info;
        if (index >= m_tracks.size()) return info;

        const auto& track = m_tracks[index];

        info.isOnline = true;
        info.localCacheId = track->id;
        info.title = track->title;
        info.artist = track->artist;
        info.album = track->album;
        info.needTags = true;
        info.lyrics = NeteaseAPIManager::GetInstance().GetLyrics(track->id); // 提前拿到歌词
        info.isValid = !info.audioUrl.empty();

        // ★ 将私有封面提出来给主程序统一下载
        info.albumArtUrl = track->albumArtUrl;

        // 获取真实播放地址
        info.audioUrl = NeteaseAPIManager::GetInstance().GetSongUrl(track->id, "exhigh", g_netease_user_cookie);
        info.isValid = !info.audioUrl.empty();

        return info;
    }

    int GetLastIndex() const override {
        auto& cfg = ManageConfig::GetInstance();
        std::string last = cfg.GetValue<std::string>("last_music_online", "");
        if (last.empty()) return 0;
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i]->id == last) return (int)i;
        }
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