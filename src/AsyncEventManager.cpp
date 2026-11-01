#include "AsyncEventManager.h"
#include <algorithm>   // std::remove_if


// ═══════════════════════════════════════════════════════════
// 类型名
// ═══════════════════════════════════════════════════════════
const char* AsyncEventTypeName(AsyncEventType t) {
    switch (t) {
    case AsyncEventType::PreLoad:        return "PreLoad";
    case AsyncEventType::PostLoad:       return "PostLoad";
    case AsyncEventType::PrePlay:        return "PrePlay";
    case AsyncEventType::PostPlay:       return "PostPlay";
    case AsyncEventType::PrePause:       return "PrePause";
    case AsyncEventType::PostPause:      return "PostPause";
    case AsyncEventType::PreStop:        return "PreStop";
    case AsyncEventType::PostStop:       return "PostStop";
    case AsyncEventType::PreSeek:        return "PreSeek";
    case AsyncEventType::PostSeek:       return "PostSeek";
    case AsyncEventType::LoadFailed:     return "LoadFailed";
    case AsyncEventType::BackendLoaded:  return "BackendLoaded";
    case AsyncEventType::BackendLoadFailed: return "BackendLoadFailed";
    case AsyncEventType::BackendPlayed:  return "BackendPlayed";
    case AsyncEventType::BackendPaused:  return "BackendPaused";
    case AsyncEventType::BackendStopped: return "BackendStopped";
    case AsyncEventType::BackendUnloaded: return "BackendUnloaded";
    case AsyncEventType::BackendSeeked:  return "BackendSeeked";
    case AsyncEventType::BackendEnded:   return "BackendEnded";
    case AsyncEventType::BackendError:   return "BackendError";
    case AsyncEventType::Custom:         return "Custom";
    }
    return "Unknown";
}

// ═══════════════════════════════════════════════════════════
AsyncEventManager& AsyncEventManager::GetInstance() {
    static AsyncEventManager inst;
    return inst;
}

AsyncEventManager::~AsyncEventManager() {
    // 保险：如果用户忘记 Stop，这里兜一下
    if (running_.load()) Stop();
}

// ── 生命周期 ──
void AsyncEventManager::Start() {
    std::lock_guard<std::mutex> lock(queueMtx_);
    if (running_.load()) return;
    stopped_.store(false);
    running_.store(true);
    worker_ = std::thread([this] { WorkerLoop(); });
}

void AsyncEventManager::Stop() {
    {
        std::lock_guard<std::mutex> lock(queueMtx_);
        if (!running_.load()) return;
        stopped_.store(true);
    }
    queueCv_.notify_all();
    if (worker_.joinable()) worker_.join();
    running_.store(false);

    // 清空未执行队列
    {
        std::lock_guard<std::mutex> lock(queueMtx_);
        queue_.clear();
    }
    // 清空主线程回调
    {
        std::lock_guard<std::mutex> lock(mainCbMtx_);
        mainCbs_.clear();
    }
}

// ── 订阅 ──
// 注册─────────────────────────────────────────────────────────
SubscriptionHandle AsyncEventManager::Subscribe(AsyncEventType type, AsyncEventHandler handler) {
    return Subscribe(type, std::move(handler), nullptr);
}

SubscriptionHandle AsyncEventManager::Subscribe(AsyncEventType type, AsyncEventHandler handler,
    AsyncFallbackHandler onFail) {
    if (!handler) return 0;                                 // ★ 返回 0 = 无效句柄
    auto entry = std::make_shared<HandlerEntry>();
    entry->handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);   // ★ 分配句柄
    entry->handler = std::move(handler);
    entry->fallback = std::move(onFail);

    SubscriptionHandle h = entry->handle;

    std::lock_guard<std::mutex> lock(subMtx_);
    subscriptions_[type].push_back(std::move(entry));
    return h;                                                // ★ 返回句柄
}

// 注销─────────────────────────────────────────────────────────
void AsyncEventManager::Unsubscribe(SubscriptionHandle handle) {
    if (handle == 0) return;

    std::lock_guard<std::mutex> lock(subMtx_);
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [handle](const std::shared_ptr<HandlerEntry>& e) {
                    return e->handle == handle;
                }),
            vec.end());
    }
}

void AsyncEventManager::SetTypeFallback(AsyncEventType type, AsyncFallbackHandler h) {
    std::lock_guard<std::mutex> lock(fbMtx_);
    typeFallback_[type] = std::move(h);
}

void AsyncEventManager::SetGlobalFallback(AsyncFallbackHandler h) {
    std::lock_guard<std::mutex> lock(fbMtx_);
    globalFallback_ = std::move(h);
}

// ── 投递 ──
void AsyncEventManager::Post(AsyncEventType type, AsyncEventPayload payload) {
    Post(type, std::move(payload), nullptr, CallbackThread::Main);
}

void AsyncEventManager::Post(AsyncEventType type, AsyncEventPayload payload,
    AsyncCompletionCallback onComplete,
    CallbackThread cbThread) {
    if (stopped_.load()) return;   // Stop 之后不再接收

    EventJob job;
    job.type = type;
    job.payload = std::make_shared<AsyncEventPayload>(std::move(payload));
    job.onComplete = std::move(onComplete);
    job.cbThread = cbThread;

    {
        std::lock_guard<std::mutex> lock(queueMtx_);
        queue_.push_back(std::move(job));
    }
    queueCv_.notify_one();
}

// ── 主线程 drain ──
void AsyncEventManager::DrainMainThread() {
    std::deque<MainCb> local;
    {
        std::lock_guard<std::mutex> lock(mainCbMtx_);
        if (mainCbs_.empty()) return;
        local.swap(mainCbs_);
    }
    for (auto& cb : local) {
        try { cb.fn(); }
        catch (...) {}
    }
}

// ═══════════════════════════════════════════════════════════
// Worker
// ═══════════════════════════════════════════════════════════
void AsyncEventManager::WorkerLoop() {
    while (true) {
        EventJob job;
        {
            std::unique_lock<std::mutex> lock(queueMtx_);
            queueCv_.wait(lock, [this] {
                return stopped_.load() || !queue_.empty();
                });
            if (queue_.empty()) {
                if (stopped_.load()) return;
                continue;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        RunJob(job);
    }
}

void AsyncEventManager::RunJob(EventJob& job) {
    // 快照订阅表（防止执行中修改导致迭代器失效）
    std::vector<std::shared_ptr<HandlerEntry>> handlers;
    {
        std::lock_guard<std::mutex> lock(subMtx_);
        auto it = subscriptions_.find(job.type);
        if (it != subscriptions_.end()) handlers = it->second;
    }

    AsyncFallbackHandler typeFb, globalFb;
    {
        std::lock_guard<std::mutex> lock(fbMtx_);
        auto it = typeFallback_.find(job.type);
        if (it != typeFallback_.end()) typeFb = it->second;
        globalFb = globalFallback_;
    }

    bool anyFailed = false;
    bool anyHandled = false;
    auto& payload = *job.payload;

    // ── 执行所有 handler（失败隔离）──
    for (auto& entry : handlers) {
        bool thisFailed = false;
        std::string reason;

        try {
            EventOutcome r = entry->handler(payload);
            if (r == EventOutcome::Handled)      anyHandled = true;
            else if (r == EventOutcome::Failed) { thisFailed = true; reason = "returned Failed"; }
            // Skipped：什么都不做
        }
        catch (const std::exception& e) { thisFailed = true; reason = std::string("exception: ") + e.what(); }
        catch (...) { thisFailed = true; reason = "unknown exception"; }

        if (thisFailed) {
            anyFailed = true;
            // 三级兜底
            if (entry->fallback) { try { entry->fallback(payload, reason); } catch (...) {} }
            if (typeFb) { try { typeFb(payload, reason); } catch (...) {} }
            if (globalFb) { try { globalFb(payload, reason); } catch (...) {} }
        }
    }

    // ── 完成回调 ──
    if (!job.onComplete) return;

    if (job.cbThread == CallbackThread::Worker) {
        try { job.onComplete(payload, anyFailed, anyHandled); }
        catch (...) {}
    }
    else {
        // 投到主线程（捕获 shared_ptr 保持存活）
        auto pPtr = job.payload;
        auto cb = job.onComplete;
        bool af = anyFailed, ah = anyHandled;
        std::lock_guard<std::mutex> lock(mainCbMtx_);
        mainCbs_.push_back(MainCb{
            [pPtr, cb, af, ah]() {
                try { cb(*pPtr, af, ah); }
 catch (...) {}
}
            });
    }
}