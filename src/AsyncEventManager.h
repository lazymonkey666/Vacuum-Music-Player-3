#pragma once
#include <platform/platform.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using SubscriptionHandle = uint64_t;
// ═══════════════════════════════════════════════════════════
// 事件类型
// ═══════════════════════════════════════════════════════════
enum class AsyncEventType {
    // ── 业务事件（状态机/主线程发起）──
    PreLoad,       // 加载前（读元数据、歌词、封面）
    PostLoad,      // 加载成功
    PrePlay,       // 播放前
    PostPlay,      // 播放启动后（AMLL / SMTC / 桌面歌词）
    PrePause, PostPause,
    PreStop, PostStop,
    PreSeek, PostSeek,
    LoadFailed,    // 加载失败（触发兜底 + UI 提示）

    // ── 后端事件（后端 worker 发起）──
    BackendLoaded, BackendLoadFailed,
    BackendPlayed, BackendPaused, BackendStopped,
    BackendUnloaded,
    BackendSeeked, BackendEnded,
    BackendError,

    // ── 扩展 ──
    Custom,

    ExternalTogglePlay, 
    ExternalNext,
    ExternalPrev,
    ExternalSeek,     
};

const char* AsyncEventTypeName(AsyncEventType t);

// ═══════════════════════════════════════════════════════════
// 载荷（允许 handler 就地改写）
// ═══════════════════════════════════════════════════════════
struct AsyncEventPayload {
    std::string filePathUtf8;
    PathType    widePath;

    // 元数据（PreLoad 时为空，由订阅者填充）
    std::string title, artist, album, lyrics;
    std::vector<unsigned char> albumArt;

    // 时序
    uint64_t durationMs = 0;
    uint64_t positionMs = 0;

    // 上下文
    int      trackIndex = -1;
    uint64_t extraInt = 0;    // 常用于 taskId
    std::string extraString;

    // 错误信息
    std::string errorMsg;
};

// ═══════════════════════════════════════════════════════════
// Handler 返回
// ═══════════════════════════════════════════════════════════
enum class EventOutcome {
    Handled,   // 成功
    Skipped,   // 跳过（不算失败，例如 AMLL 未连接）
    Failed,    // 失败（触发兜底）
};

// ═══════════════════════════════════════════════════════════
// 回调线程
// ═══════════════════════════════════════════════════════════
enum class CallbackThread { Worker, Main };

// ═══════════════════════════════════════════════════════════
// 签名
// ═══════════════════════════════════════════════════════════
using AsyncEventHandler = std::function<EventOutcome(AsyncEventPayload&)>;
using AsyncFallbackHandler = std::function<void(AsyncEventPayload&, const std::string& reason)>;
using AsyncCompletionCallback =
std::function<void(AsyncEventPayload&, bool anyFailed, bool anyHandled)>;

// ═══════════════════════════════════════════════════════════
// 管理器
// ═══════════════════════════════════════════════════════════
class AsyncEventManager {
public:
    static AsyncEventManager& GetInstance();

    // ── 生命周期 ──
    void Start();
    void Stop();

    // ── 订阅（同一事件多个订阅者，串行执行）──
    SubscriptionHandle Subscribe(AsyncEventType type, AsyncEventHandler handler);
    SubscriptionHandle Subscribe(AsyncEventType type, AsyncEventHandler handler,
        AsyncFallbackHandler onFail);

    void Unsubscribe(SubscriptionHandle handle);//取消订阅

    // ── 三级兜底 ──
    void SetTypeFallback(AsyncEventType type, AsyncFallbackHandler handler);
    void SetGlobalFallback(AsyncFallbackHandler handler);

    // ── 投递（fire-and-forget）──
    void Post(AsyncEventType type, AsyncEventPayload payload);

    // ── 投递（带完成回调）──
    void Post(AsyncEventType type, AsyncEventPayload payload,
        AsyncCompletionCallback onComplete,
        CallbackThread cbThread = CallbackThread::Main);

    // ── 主线程每帧调用（执行 CallbackThread::Main 的回调）──
    void DrainMainThread();

private:
    AsyncEventManager() = default;
    ~AsyncEventManager();
    AsyncEventManager(const AsyncEventManager&) = delete;
    AsyncEventManager& operator=(const AsyncEventManager&) = delete;

    struct HandlerEntry {
        SubscriptionHandle   handle = 0;      // ★ 新增
        AsyncEventHandler    handler;
        AsyncFallbackHandler fallback;   // 可空
    };
    struct EventJob {
        AsyncEventType                    type;
        std::shared_ptr<AsyncEventPayload> payload;
        AsyncCompletionCallback           onComplete;   // 可空
        CallbackThread                    cbThread = CallbackThread::Main;
    };
    struct MainCb { std::function<void()> fn; };

    void WorkerLoop();
    void RunJob(EventJob& job);

    // 队列
    std::atomic<bool>        running_{ false };
    std::atomic<bool>        stopped_{ false };
    std::thread              worker_;
    std::mutex               queueMtx_;
    std::condition_variable  queueCv_;
    std::deque<EventJob>     queue_;

    // 主线程回调队列
    std::mutex               mainCbMtx_;
    std::deque<MainCb>       mainCbs_;

    // 订阅
    std::mutex               subMtx_;
    std::unordered_map<AsyncEventType,
        std::vector<std::shared_ptr<HandlerEntry>>> subscriptions_;
    std::atomic<SubscriptionHandle> nextHandle_{ 1 };

    // 兜底
    std::mutex               fbMtx_;
    std::unordered_map<AsyncEventType, AsyncFallbackHandler> typeFallback_;
    AsyncFallbackHandler     globalFallback_;
};