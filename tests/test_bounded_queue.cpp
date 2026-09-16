// ---------------------------------------------------------------------------
// Unit tests for core/bounded_queue.hpp — the bounded, drop-oldest queue that
// keeps capture from ever blocking inference (INV-031).
//
// These run on the host with no hardware (AGENTS.md §9).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <string>
#include <thread>

#include "core/bounded_queue.hpp"

using lumina::core::BoundedQueue;

TEST_CASE("BoundedQueue never grows past its capacity") {
    BoundedQueue<int> queue(3);
    for (int i = 0; i < 3; ++i) {
        queue.push(i);
    }
    CHECK(queue.size() == 3);
    CHECK(queue.capacity() == 3);
}

TEST_CASE("BoundedQueue drops the oldest item when full") {
    BoundedQueue<int> queue(2);
    queue.push(1);
    queue.push(2);
    queue.push(3);  // capacity reached: 1 is evicted, then 3 is appended

    int out = 0;
    CHECK(queue.tryPop(out));
    CHECK(out == 2);  // oldest surviving item
    CHECK(queue.tryPop(out));
    CHECK(out == 3);
    CHECK_FALSE(queue.tryPop(out));  // now empty
}

TEST_CASE("tryPop returns false on an empty queue") {
    BoundedQueue<std::string> queue(1);
    std::string out;
    CHECK_FALSE(queue.tryPop(out));
}

TEST_CASE("a zero capacity is clamped to one") {
    BoundedQueue<int> queue(0);
    CHECK(queue.capacity() == 1);
}

TEST_CASE("waitPop unblocks and returns false after close()") {
    BoundedQueue<int> queue(1);

    // Closing from another thread proves waitPop() is woken rather than blocked
    // forever; the join() guarantees the thread finished before we assert.
    std::thread closer([&queue] { queue.close(); });

    int out = 0;
    const bool gotItem = queue.waitPop(out);
    closer.join();

    CHECK_FALSE(gotItem);
}

TEST_CASE("waitPop drains queued items before reporting closed") {
    BoundedQueue<int> queue(4);
    queue.push(42);
    queue.close();

    int out = 0;
    CHECK(queue.waitPop(out));  // queued item is still delivered
    CHECK(out == 42);
    CHECK_FALSE(queue.waitPop(out));  // then closed and empty
}
