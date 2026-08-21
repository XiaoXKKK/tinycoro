#include "tinycoro/queue.h"
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
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
    while (q.push(pushed))
        ++pushed;
    EXPECT_EQ(pushed, 3); // 4-1 = 3 slots usable

    auto v = q.pop();
    EXPECT_TRUE(v.has_value());
    EXPECT_TRUE(q.push(99)); // one slot freed
}

TEST(SPSCQueueTest, ConcurrentSPSC) {
    SPSCQueue<int, 1024> q;
    constexpr int N = 10000;
    std::vector<int> received;
    received.reserve(N);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!q.push(i))
                std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        while (received.size() < N) {
            auto v = q.pop();
            if (v) {
                received.push_back(*v);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    ASSERT_EQ(received.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i)
        EXPECT_EQ(received[static_cast<std::size_t>(i)], i);
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

    auto seen = std::make_unique<std::atomic<unsigned int>[]>(TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        seen[i].store(0, std::memory_order_relaxed);
    std::atomic<int> consumed_count{0};
    std::atomic<bool> out_of_range{false};

    std::vector<std::thread> producers, consumers;

    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            int start = p * PER_PRODUCER;
            for (int i = start; i < start + PER_PRODUCER; ++i) {
                while (!q.push(i))
                    std::this_thread::yield();
            }
        });
    }

    for (int c = 0; c < CONSUMERS; ++c) {
        consumers.emplace_back([&] {
            int val;
            for (;;) {
                if (q.pop(val)) {
                    if (val < 0)
                        return;
                    if (val >= TOTAL) {
                        out_of_range.store(true, std::memory_order_relaxed);
                    } else {
                        seen[val].fetch_add(1, std::memory_order_relaxed);
                    }
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& producer : producers)
        producer.join();
    for (int i = 0; i < CONSUMERS; ++i) {
        while (!q.push(-1))
            std::this_thread::yield();
    }
    for (auto& consumer : consumers)
        consumer.join();

    EXPECT_FALSE(out_of_range.load(std::memory_order_relaxed));
    EXPECT_EQ(consumed_count.load(std::memory_order_relaxed), TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        EXPECT_EQ(seen[i].load(std::memory_order_relaxed), 1u);
}
