#include "tinycoro/thread_pool.h"
#include <atomic>
#include <chrono>
#include <exception>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace tinycoro;

// ---- ThreadPool --------------------------------------------------------

TEST(ThreadPoolTest, RejectsZeroWorkers) {
    EXPECT_THROW(ThreadPool pool(0), std::invalid_argument);
}

TEST(ThreadPoolTest, RejectsSubmitAfterShutdown) {
    ThreadPool pool(1);
    pool.shutdown();
    EXPECT_FALSE(pool.submit([] {}));
}

TEST(ThreadPoolTest, SubmitAndExecute) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        while (!pool.submit([&] { counter.fetch_add(1, std::memory_order_relaxed); })) {
        }
    }

    // Give workers time to drain
    pool.shutdown();
    EXPECT_EQ(counter.load(), N);
}

TEST(ThreadPoolTest, ConcurrentSubmitExecutesEveryTaskOnce) {
    ThreadPool pool(4);
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 1000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;
    auto seen = std::make_unique<std::atomic<unsigned int>[]>(TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        seen[i].store(0, std::memory_order_relaxed);

    std::vector<std::thread> producers;
    for (int producer = 0; producer < PRODUCERS; ++producer) {
        producers.emplace_back([&, producer] {
            const int begin = producer * PER_PRODUCER;
            const int end = begin + PER_PRODUCER;
            for (int id = begin; id < end; ++id) {
                while (
                    !pool.submit([&, id] { seen[id].fetch_add(1, std::memory_order_relaxed); })) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& producer : producers)
        producer.join();

    pool.shutdown();
    for (int id = 0; id < TOTAL; ++id)
        EXPECT_EQ(seen[id].load(std::memory_order_relaxed), 1u);
}

TEST(ThreadPoolTest, CapturesTaskExceptions) {
    ThreadPool pool(1);
    ASSERT_TRUE(pool.submit([] { throw std::runtime_error("worker failure"); }));
    pool.shutdown();

    auto errors = pool.take_errors();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_THROW(std::rethrow_exception(errors.front()), std::runtime_error);
    EXPECT_TRUE(pool.take_errors().empty());
}
