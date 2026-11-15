#pragma once
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <functional>
#include <type_traits>

// ── 1. 基础数据类型（int, bool, uint64_t, float 等）→ 原子变量 ──
template<typename T>
class StateVar {
    static_assert(std::is_trivially_copyable_v<T>, "Complex types must use StateObject.");
    std::atomic<T> m_val;
public:
    explicit StateVar(T init = T{}) : m_val(init) {}
    // 统一接口
    inline T Get() const { return m_val.load(std::memory_order_relaxed); }
    inline void Set(T v) { m_val.store(v, std::memory_order_relaxed); }

    // 语法糖：像普通变量一样直接使用
    inline operator T() const { return Get(); }
    inline StateVar& operator=(T v) { Set(v); return *this; }
    inline StateVar& operator+=(T v) { m_val.fetch_add(v); return *this; }
    inline StateVar& operator-=(T v) { m_val.fetch_sub(v); return *this; }

    inline T operator++() { return m_val.fetch_add(1) + 1; }
    inline T operator++(int) { return m_val.fetch_add(1); }
    inline T operator--() { return m_val.fetch_sub(1) - 1; }
    inline T operator--(int) { return m_val.fetch_sub(1); }
};

// ── 2. 复杂数据类型（std::string, struct 等）→ 读写锁 ──
template<typename T>
class StateObject {
    mutable std::shared_mutex m_mtx;
    T m_val;
public:
    explicit StateObject(T init = T{}) : m_val(std::move(init)) {}

    // 统一接口
    T Get() const {
        std::shared_lock lock(m_mtx);
        return m_val;
    }
    void Set(const T& v) {
        std::unique_lock lock(m_mtx);
        m_val = v;
    }
    void Set(T&& v) {
        std::unique_lock lock(m_mtx);
        m_val = std::move(v);
    }

    template<typename Func>
    void Modify(Func&& func) {
        std::unique_lock lock(m_mtx);
        func(m_val);
    }

    StateObject& operator=(const T& v) {
        Set(v);
        return *this;
    }

    StateObject& operator=(T&& v) {
        Set(std::move(v));
        return *this;
    }
};