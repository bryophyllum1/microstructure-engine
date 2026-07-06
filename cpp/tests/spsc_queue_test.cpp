#include "msengine/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

namespace msengine {
namespace {

TEST(SpscQueue, CapacityRoundsUpToPowerOfTwo) {
    SpscQueue<int> q(100);
    EXPECT_EQ(q.capacity(), 128u);
    SpscQueue<int> q2(128);
    EXPECT_EQ(q2.capacity(), 128u);
}

TEST(SpscQueue, PopOnEmptyFails) {
    SpscQueue<int> q(4);
    int out = 0;
    EXPECT_FALSE(q.try_pop(out));
}

TEST(SpscQueue, PushPopPreservesFifoOrder) {
    SpscQueue<int> q(8);
    for (int i = 1; i <= 5; ++i) EXPECT_TRUE(q.try_push(std::move(i)));

    int out = 0;
    for (int i = 1; i <= 5; ++i) {
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_FALSE(q.try_pop(out));
}

TEST(SpscQueue, PushOnFullFails) {
    SpscQueue<int> q(4);  // capacity 4
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.try_push(std::move(i)));
    int extra = 99;
    EXPECT_FALSE(q.try_push(std::move(extra)));

    int out = 0;
    ASSERT_TRUE(q.try_pop(out));      // free one slot
    EXPECT_TRUE(q.try_push(std::move(extra)));  // now it fits
}

TEST(SpscQueue, WrapsAroundManyTimes) {
    SpscQueue<int> q(4);
    int out = 0;
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(q.try_push(std::move(i)));
        ASSERT_TRUE(q.try_pop(out));
        ASSERT_EQ(out, i);
    }
}

TEST(SpscQueue, MoveOnlyTypesSupported) {
    SpscQueue<std::unique_ptr<int>> q(4);
    ASSERT_TRUE(q.try_push(std::make_unique<int>(42)));
    std::unique_ptr<int> out;
    ASSERT_TRUE(q.try_pop(out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(*out, 42);
}

// Two real threads hammering the queue: every value pushed must arrive
// exactly once, in order. Run count is large enough to exercise wraps,
// full-queue and empty-queue paths under genuine concurrency.
TEST(SpscQueue, ConcurrentStressPreservesEveryItem) {
    constexpr std::uint64_t kCount = 1'000'000;
    SpscQueue<std::uint64_t> q(1024);

    std::uint64_t sum = 0;
    std::uint64_t last = 0;
    bool ordered = true;

    std::thread consumer([&] {
        std::uint64_t received = 0;
        std::uint64_t value = 0;
        while (received < kCount) {
            if (q.try_pop(value)) {
                if (value < last) ordered = false;
                last = value;
                sum += value;
                ++received;
            }
        }
    });

    for (std::uint64_t i = 1; i <= kCount; ++i) {
        while (!q.try_push(std::uint64_t{i})) {
            std::this_thread::yield();  // consumer will drain
        }
    }
    consumer.join();

    EXPECT_TRUE(ordered);
    EXPECT_EQ(sum, kCount * (kCount + 1) / 2);  // arithmetic series: nothing lost
}

}  // namespace
}  // namespace msengine
