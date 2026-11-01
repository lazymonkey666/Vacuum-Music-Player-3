#include "PlaybackService.h"
#include "AsyncEventManager.h"
#include <platform/platform.h>

class SmtcService : public IPlaybackService {
    std::vector<SubscriptionHandle> m_handles;

public:
    const char* Id() const override { return "smtc"; }
    const char* DisplayName() const override { return "系统媒体控制"; }

    bool Init() override {
        auto& em = AsyncEventManager::GetInstance();

        m_handles.push_back(em.Subscribe(AsyncEventType::PostPlay,
            [](AsyncEventPayload& p) -> EventOutcome {
                std::vector<unsigned char> cover = ConvertImageToJPEG(p.albumArt);
                if (cover.empty()) cover = p.albumArt;
                UpdateSMTC(p.title, p.artist, p.album, cover, p.durationMs);
                SetSMTCPlaybackStatus(PlaybackStatus::Playing);
                return EventOutcome::Handled;
            }));

        m_handles.push_back(em.Subscribe(AsyncEventType::BackendPaused,
            [](AsyncEventPayload&) -> EventOutcome {
                SetSMTCPlaybackStatus(PlaybackStatus::Paused);
                return EventOutcome::Handled;
            }));

        m_handles.push_back(em.Subscribe(AsyncEventType::BackendPlayed,
            [](AsyncEventPayload&) -> EventOutcome {
                SetSMTCPlaybackStatus(PlaybackStatus::Playing);
                return EventOutcome::Handled;
            }));

        m_handles.push_back(em.Subscribe(AsyncEventType::BackendStopped,
            [](AsyncEventPayload&) -> EventOutcome {
                SetSMTCPlaybackStatus(PlaybackStatus::Stopped);
                return EventOutcome::Handled;
            }));

        

        return true;
    }

    void Poll(uint64_t currentMs, bool /*isPlaying*/) override {
        // 只更新进度，状态由事件驱动
        UpdateSMTCProgress(currentMs);
    }

    void Shutdown() override {
        auto& em = AsyncEventManager::GetInstance();
        for (auto h : m_handles) em.Unsubscribe(h);
        m_handles.clear();
    }
};

std::unique_ptr<IPlaybackService> CreateSmtcService() {
    return std::make_unique<SmtcService>();
}