#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace lumina::core {

// A thread-safe queue with a hard capacity. When full, pushing a new item first
// discards the OLDEST item, so producers (capture) never block and consumers
// (inference) always work on the newest data (INV-031). Construct with capacity 1
// for the "keep only the latest frame" handoff described in RAW_PLAN.md §3.
//
// C++ note (for Java readers): this is a class template (the C++ analogue of Java
// generics) resolved entirely at compile time, so there is no type erasure. The
// mutex and condition_variable are members, i.e. RAII: `std::lock_guard` and
// `std::unique_lock` release the lock automatically on scope exit, including on
// exception, so an unlock can never be forgotten.
template <typename T>
class BoundedQueue {
public:
    // `capacity` is clamped to at least 1 so the queue is always usable.
    explicit BoundedQueue(std::size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

    // The queue owns a mutex; copying it is meaningless and unsafe.
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // Add an item. Never blocks: at capacity, the oldest item is dropped first.
    void push(T value) {
        {
            std::lock_guard lock(m_mutex);
            if (m_items.size() >= m_capacity) {
                m_items.pop_front();  // drop the stale item
            }
            m_items.push_back(std::move(value));
        }
        m_cond.notify_one();
    }

    // Non-blocking pop. Returns false when the queue is empty.
    [[nodiscard]] bool tryPop(T& out) {
        std::lock_guard lock(m_mutex);
        if (m_items.empty()) {
            return false;
        }
        out = std::move(m_items.front());
        m_items.pop_front();
        return true;
    }

    // Blocking pop. Returns false only when the queue was closed and fully drained,
    // so worker threads can exit cleanly instead of spinning.
    [[nodiscard]] bool waitPop(T& out) {
        std::unique_lock lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_items.empty() || m_closed; });
        if (m_items.empty()) {
            return false;  // closed and empty
        }
        out = std::move(m_items.front());
        m_items.pop_front();
        return true;
    }

    // Wake every blocked waiter. Items already queued remain drainable.
    void close() {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_cond.notify_all();
    }

    // Current number of queued items (a thread-safe snapshot).
    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(m_mutex);
        return m_items.size();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock(m_mutex);
        return m_items.empty();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

private:
    mutable std::mutex m_mutex;      // guards m_items and m_closed
    std::condition_variable m_cond;  // signalled on push() and close()
    std::deque<T> m_items;           // front = oldest item
    std::size_t m_capacity;          // always >= 1
    bool m_closed = false;           // set by close()
};

}  // namespace lumina::core
