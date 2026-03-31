#include "tinycoro/queue.h"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

using namespace tinycoro;

// ---- SPSCQueue ---------------------------------------------------------

TEST(SPSCQueueTest, PushPopSingleThread) {
    SPSCQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_EQ(q.size(), 2u);

    auto v = q.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1);

    v = q.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 2);

    EXPECT_FALSE(q.pop().has_value()); // empty
}

TEST(SPSCQueueTest, Full) {
    SPSCQueue<int, 4> q; // capacity 4, usable slots = 3 (ring buffer full condition)
    // Fill until full
    int pushed = 0;
    while (q.push(pushed)) ++pushed;
    EXPECT_EQ(pushed, 3); // 4-1 = 3 slots usable

    auto v = q.pop();
    EXPECT_TRUE(v.has_value());
    EXPECT_TRUE(q.push(99)); // one slot freed
}

TEST(SPSCQueueTest, ConcurrentSPSC) {
    SPSCQueue<int, 1024> q;
    constexpr int N = 10000;
    std::atomic<int> sum{0};

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!q.push(i)) std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        int received = 0;
        while (received < N) {
            auto v = q.pop();
            if (v) {
                sum.fetch_add(*v, std::memory_order_relaxed);
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();

    int expected = N * (N - 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

// ---- MPMCQueue ---------------------------------------------------------

TEST(MPMCQueueTest, PushPopSingleThread) {
    MPMCQueue<int, 8> q;
    EXPECT_TRUE(q.push(42));
    int val = 0;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_FALSE(q.pop(val)); // empty
}

TEST(MPMCQueueTest, ConcurrentMPMC) {
    MPMCQueue<int, 4096> q;
    constexpr int PRODUCERS = 4;
    constexpr int CONSUMERS = 4;
    constexpr int PER_PRODUCER = 2500; // total = 10000
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    std::atomic<long long> sum{0};
    std::atomic<int> consumed_count{0};

    std::vector<std::thread> producers, consumers;

    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            int start = p * PER_PRODUCER;
            for (int i = start; i < start + PER_PRODUCER; ++i) {
                while (!q.push(i)) std::this_thread::yield();
            }
        });
    }

    for (int c = 0; c < CONSUMERS; ++c) {
        consumers.emplace_back([&] {
            int val;
            while (consumed_count.load(std::memory_order_relaxed) < TOTAL) {
                if (q.pop(val)) {
                    sum.fetch_add(val, std::memory_order_relaxed);
                    // Increment after adding to sum to avoid early exit
                    consumed_count.fetch_add(1, std::memory_order_acq_rel);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    long long expected = static_cast<long long>(TOTAL) * (TOTAL - 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}
