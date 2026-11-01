#include "PlaybackService.h"
#include "AsyncEventManager.h"
#include <AMLLWebSocketV2.h>
#include <ManageConfig.h>

class AMLLService : public IPlaybackService {
    std::unique_ptr<AMLLWebSocketClient> m_player;
    std::string m_address;
    bool        m_translateSplit = true;
    std::vector<SubscriptionHandle> m_handles;

public:
    const char* Id() const override { return "amll"; }
    const char* DisplayName() const override { return "AMLL 歌词同步"; }

    bool Init() override {
        // ── 1. 读配置 + 建连接 ──
        auto& cfg = ManageConfig::GetInstance();
        m_address = cfg.GetValue<std::string>("amll_address", "ws://localhost:11444");
        m_translateSplit = cfg.GetValue<bool>("enable_lyric_translate_split", true);

        if (m_address.empty()) return false;
        m_player = std::make_unique<AMLLWebSocketClient>(m_address);
        m_player->start();

        // ── 2. 订阅播放事件 ──
        auto& em = AsyncEventManager::GetInstance();

        m_handles.push_back(em.Subscribe(AsyncEventType::PostPlay,
            [this](AsyncEventPayload& p) -> EventOutcome {
                if (!m_player) return EventOutcome::Failed;
                std::vector<Artist> artists = { { "ar0", p.artist } };
                m_player->sendMusicInfo("t0", p.title, "al0", p.album, artists, p.durationMs);
                if (!p.albumArt.empty()) {
                    auto jpeg = ConvertImageToJPEG(p.albumArt);
                    if (jpeg.empty()) jpeg = p.albumArt;
                    m_player->sendAlbumCoverByURI(m_player->imageToDataURI(jpeg, "image/jpeg"));
                }
                m_player->sendLyricFromLRC(p.lyrics, m_translateSplit);
                return EventOutcome::Handled;
            }));

        m_handles.push_back(em.Subscribe(AsyncEventType::BackendPaused,
            [this](AsyncEventPayload&) -> EventOutcome {
                if (m_player) m_player->sendPaused();
                return EventOutcome::Handled;
            }));

        m_handles.push_back(em.Subscribe(AsyncEventType::BackendPlayed,
            [this](AsyncEventPayload&) -> EventOutcome {
                if (m_player) m_player->sendResumed();
                return EventOutcome::Handled;
            }));

        m_handles.push_back(em.Subscribe(AsyncEventType::BackendSeeked,
            [this](AsyncEventPayload& p) -> EventOutcome {
                if (m_player) m_player->sendProgress(p.positionMs);
                return EventOutcome::Handled;
            }));

        return true;
    }

    void Shutdown() override {
        // ── 1. 先退订，避免后续事件打到已销毁的 player ──
        auto& em = AsyncEventManager::GetInstance();
        for (auto h : m_handles) em.Unsubscribe(h);
        m_handles.clear();

        // ── 2. 断连 ──
        if (m_player) {
            m_player->stop();
            m_player.reset();
        }
    }

    // ★ 新增：主程序每帧/每秒调一次，服务自己处理进度上报和反向命令
    void Poll(uint64_t currentMs, bool isPlaying) override {
        if (!m_player) return;
        m_player->sendProgress(currentMs);
        if (isPlaying) m_player->sendResumed();
        else           m_player->sendPaused();

        // 反向命令：AMLL 界面点击"暂停"等 → 转成事件回传给主程序
        AMLLCommand cmd = m_player->getNextCommand();
        switch (cmd) {
        case AMLLCommand::Pause:
        case AMLLCommand::Resume:
            AsyncEventManager::GetInstance().Post(
                AsyncEventType::ExternalTogglePlay, {});
            break;
        case AMLLCommand::Forward:
            AsyncEventManager::GetInstance().Post(
                AsyncEventType::ExternalNext, {});
            break;
        case AMLLCommand::Backward:
            AsyncEventManager::GetInstance().Post(
                AsyncEventType::ExternalPrev, {});
            break;
        case AMLLCommand::Seek: {
            uint64_t pos = m_player->getPendingSeek();
            if (pos == UINT64_MAX) break;
            AsyncEventPayload p;
            p.positionMs = pos;
            AsyncEventManager::GetInstance().Post(
                AsyncEventType::ExternalSeek, std::move(p));
            break;
        }
        default: break;
        }
    }
};

std::unique_ptr<IPlaybackService> CreateAMLLService() {
    return std::make_unique<AMLLService>();
}