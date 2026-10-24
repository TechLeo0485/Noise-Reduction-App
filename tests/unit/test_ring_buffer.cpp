#include "core/SpscRingBuffer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

using vs::SpscRingBuffer;

TEST(SpscRingBuffer, CapacityRoundsUpToPowerOfTwo) {
    EXPECT_EQ(SpscRingBuffer<float>(1000).capacity(), 1024u);
    EXPECT_EQ(SpscRingBuffer<float>(1024).capacity(), 1024u);
    EXPECT_EQ(SpscRingBuffer<float>(1).capacity(), 2u);
}

TEST(SpscRingBuffer, PushPopPreservesOrderAcrossWrap) {
    SpscRingBuffer<int> ring(8);
    std::vector<int> out(8);
    int next = 0;
    int expected = 0;
    for (int round = 0; round < 50; ++round) {
        std::vector<int> chunk(5);
        std::iota(chunk.begin(), chunk.end(), next);
        ASSERT_TRUE(ring.tryPush(chunk.data(), chunk.size()));
        next += 5;
        const std::size_t n = ring.pop(out.data(), 5);
        ASSERT_EQ(n, 5u);
        for (std::size_t i = 0; i < n; ++i) {
            EXPECT_EQ(out[i], expected++);
        }
    }
}

TEST(SpscRingBuffer, TryPushIsAllOrNothing) {
    SpscRingBuffer<int> ring(8);
    std::vector<int> data(6, 1);
    ASSERT_TRUE(ring.tryPush(data.data(), 6));
    EXPECT_FALSE(ring.tryPush(data.data(), 3)); // only 2 free
    EXPECT_EQ(ring.available(), 6u);
    EXPECT_EQ(ring.freeSpace(), 2u);
    EXPECT_TRUE(ring.tryPush(data.data(), 2));
    EXPECT_EQ(ring.freeSpace(), 0u);
}

TEST(SpscRingBuffer, SkipAndPartialPop) {
    SpscRingBuffer<int> ring(16);
    std::vector<int> data(10);
    std::iota(data.begin(), data.end(), 0);
    ring.tryPush(data.data(), data.size());
    EXPECT_EQ(ring.skip(4), 4u);
    std::vector<int> out(20);
    EXPECT_EQ(ring.pop(out.data(), 20), 6u);
    EXPECT_EQ(out[0], 4);
    EXPECT_EQ(out[5], 9);
    EXPECT_EQ(ring.skip(3), 0u);
}

TEST(SpscRingBuffer, ConcurrentProducerConsumerKeepsSequence) {
    SpscRingBuffer<std::uint32_t> ring(1024);
    constexpr std::uint32_t kTotal = 2'000'000;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        std::uint32_t next = 0;
        std::uint32_t chunk[37];
        while (next < kTotal) {
            const std::uint32_t n = std::min<std::uint32_t>(37, kTotal - next);
            for (std::uint32_t i = 0; i < n; ++i) {
                chunk[i] = next + i;
            }
            if (ring.tryPush(chunk, n)) {
                next += n;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::uint32_t expected = 0;
    std::uint32_t buffer[64];
    while (expected < kTotal) {
        const std::size_t n = ring.pop(buffer, 64);
        for (std::size_t i = 0; i < n; ++i) {
            if (buffer[i] != expected++) {
                failed = true;
            }
        }
        if (n == 0) {
            std::this_thread::yield();
        }
    }
    producer.join();
    EXPECT_FALSE(failed.load());
    EXPECT_EQ(ring.available(), 0u);
}
