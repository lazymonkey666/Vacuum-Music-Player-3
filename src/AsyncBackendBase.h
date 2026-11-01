#pragma once
#include "PlaybackBackend.h"
#include "AsyncEventManager.h"          // ★ 依赖 AsyncEventPayload / AsyncEventType
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

class AsyncBackendBase : public IPlaybackBackend {
public:
    void Shutdown() override { StopWorker(); }

    // ── 命令入口：立刻返回 taskId ──
    uint64_t RequestLoad(const PathType& path, uint64_t userTag = 0) override {
        return Enqueue(CommandType::Load, path, 0, userTag);
    }
    uint64_t RequestUnload(uint64_t userTag = 0) override {
        return Enqueue(CommandType::Unload, {}, 0, userTag);
    }
    uint64_t RequestPlay(uint64_t userTag = 0) override {
        return Enqueue(CommandType::Play, {}, 0, userTag);
    }
    uint64_t RequestPause(uint64_t userTag = 0) override {
        return Enqueue(CommandType::Pause, {}, 0, userTag);
    }
    uint64_t RequestStop(uint64_t userTag = 0) override {
        return Enqueue(CommandType::Stop, {}, 0, userTag);
    }
    uint64_t RequestSeek(uint64_t ms, uint64_t userTag = 0) override {
        return Enqueue(CommandType::Seek, {}, ms, userTag);
    }

    // ── 状态读取 ──
    bool     IsLoaded()      const override { return loaded_.load(std::memory_order_relaxed); }
    bool     IsPlaying()     const override { return playing_.load(std::memory_order_relaxed); }
    bool     IsAtEnd()       const override { return atEnd_.load(std::memory_order_relaxed); }
    uint64_t GetPositionMs() const override { return positionMs_.load(std::memory_order_relaxed); }
    uint64_t GetDurationMs() const override { return durationMs_.load(std::memory_order_relaxed); }

protected:
    enum class CommandType { Load, Unload, Play, Pause, Stop, Seek };

    struct Command {
        CommandType type;
        uint64_t    taskId;
        uint64_t    userTag;
        PathType    path;
        uint64_t    seekMs;
    };

    // ── 子类实现 ──
    virtual void Execute(const Command& cmd) = 0;
    virtual void PollIdleState() {}

    // ── 生命周期 ──
    void StartWorker() {
        shutdown_.store(false);
        worker_ = std::thread([this] { WorkerLoop(); });
    }
    void StopWorker() {
        {
            std::lock_guard<std::mutex> lock(queueMtx_);
            shutdown_.store(true);
        }
        queueCv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    // ── 状态更新 ──
    void SetLoaded(bool v) { loaded_.store(v, std::memory_order_relaxed); }
    void SetPlaying(bool v) { playing_.store(v, std::memory_order_relaxed); }
    void SetAtEnd(bool v) { atEnd_.store(v, std::memory_order_relaxed); }
    void SetPositionMs(uint64_t v) { positionMs_.store(v, std::memory_order_relaxed); }
    void SetDurationMs(uint64_t v) { durationMs_.store(v, std::memory_order_relaxed); }

    // ── Payload helper ──
    AsyncEventPayload MakePayload(const Command& cmd) const {
        AsyncEventPayload p;
        p.extraInt = cmd.taskId;
        p.trackIndex = (int)cmd.userTag;
        p.widePath = cmd.path;
        return p;
    }

    // ── 快捷发事件 ──
    void EmitBackendEvent(AsyncEventType type, AsyncEventPayload payload) {
        AsyncEventManager::GetInstance().Post(type, std::move(payload));
    }
    void EmitError(const Command& cmd, const std::string& msg) {
        auto p = MakePayload(cmd);
        p.errorMsg = msg;
        AsyncEventManager::GetInstance().Post(AsyncEventType::BackendError, std::move(p));
    }
    // 空闲状态无 cmd 上下文时用
    void EmitIdleEvent(AsyncEventType type,
        uint64_t positionMs = 0, uint64_t durationMs = 0) {
        AsyncEventPayload p;
        p.positionMs = positionMs;
        p.durationMs = durationMs;
        AsyncEventManager::GetInstance().Post(type, std::move(p));
    }

private:
    uint64_t Enqueue(CommandType type, PathType path, uint64_t seekMs, uint64_t userTag) {
        uint64_t tid;
        {
            std::lock_guard<std::mutex> lock(idMtx_);
            tid = nextTaskId_++;
        }
        Command cmd{ type, tid, userTag, std::move(path), seekMs };

        switch (type) {
        case CommandType::Load:   latestLoad_.store(tid);   break;
        case CommandType::Unload: latestUnload_.store(tid); break;
        case CommandType::Play:   latestPlay_.store(tid);   break;
        case CommandType::Pause:  latestPause_.store(tid);  break;
        case CommandType::Stop:   latestStop_.store(tid);   break;
        case CommandType::Seek:   latestSeek_.store(tid);   break;
        }
        {
            std::lock_guard<std::mutex> lock(queueMtx_);
            queue_.push_back(std::move(cmd));
        }
        queueCv_.notify_one();
        return tid;
    }

    void WorkerLoop() {
        while (true) {
            std::optional<Command> cmd;
            {
                std::unique_lock<std::mutex> lock(queueMtx_);
                queueCv_.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { return shutdown_.load() || !queue_.empty(); });
                if (shutdown_.load() && queue_.empty()) return;
                if (!queue_.empty()) {
                    cmd = std::move(queue_.front());
                    queue_.pop_front();
                }
            }

            if (!cmd) {
                try { PollIdleState(); }
                catch (...) {}
                continue;
            }

            // ── supersede：过期命令静默丢弃 ──
            uint64_t latest = 0;
            switch (cmd->type) {
            case CommandType::Load:   latest = latestLoad_.load();   break;
            case CommandType::Unload: latest = latestUnload_.load(); break;
            case CommandType::Play:   latest = latestPlay_.load();   break;
            case CommandType::Pause:  latest = latestPause_.load();  break;
            case CommandType::Stop:   latest = latestStop_.load();   break;
            case CommandType::Seek:   latest = latestSeek_.load();   break;
            }
            if (cmd->taskId < latest) continue;   // 丢弃

            // ── 执行 + 异常兜底 ──
            try {
                Execute(*cmd);
            }
            catch (const std::exception& e) {
                EmitError(*cmd, std::string("exception: ") + e.what());
            }
            catch (...) {
                EmitError(*cmd, "unknown exception");
            }

            // ── 执行后立即 poll 一次（让 Ended/Position 早一帧生效）──
            try { PollIdleState(); }
            catch (...) {}
        }
    }

    std::mutex               queueMtx_;
    std::condition_variable  queueCv_;
    std::deque<Command>      queue_;
    std::atomic<bool>        shutdown_{ false };
    std::thread              worker_;

    std::mutex   idMtx_;
    uint64_t     nextTaskId_ = 1;

    std::atomic<uint64_t> latestLoad_{ 0 }, latestUnload_{ 0 }, latestPlay_{ 0 },
        latestPause_{ 0 }, latestStop_{ 0 }, latestSeek_{ 0 };

    std::atomic<bool>     loaded_{ false }, playing_{ false }, atEnd_{ false };
    std::atomic<uint64_t> positionMs_{ 0 }, durationMs_{ 0 };
};