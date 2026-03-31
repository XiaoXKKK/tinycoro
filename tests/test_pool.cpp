#include "tinycoro/thread_pool.h"
#include "tinycoro/coroutine_pool.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>

using namespace tinycoro;

// ---- ThreadPool --------------------------------------------------------

TEST(ThreadPoolTest, SubmitAndExecute) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        while (!pool.submit([&] {
            counter.fetch_add(1, std::memory_order_relaxed);
        })) {}
    }

    // Give workers time to drain
    pool.shutdown();
    EXPECT_EQ(counter.load(), N);
}

TEST(ThreadPoolTest, ConcurrentSubmit) {
    ThreadPool pool(4);
    std::atomic<long long> sum{0};
    constexpr int N = 1000;

    for (int i = 0; i < N; ++i) {
        while (!pool.submit([&, i] {
            sum.fetch_add(i, std::memory_order_relaxed);
        })) {}
    }

    pool.shutdown();
    EXPECT_EQ(sum.load(), static_cast<long long>(N) * (N - 1) / 2);
}

// ---- CoroutinePool -----------------------------------------------------

TEST(CoroutinePoolTest, AcquireAndRelease) {
    CoroutinePool pool(4);
    EXPECT_EQ(pool.pool_size(), 0u);

    bool ran = false;
    auto coro = pool.acquire([&] { ran = true; });
    coro->resume();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(coro->is_done());

    pool.release(std::move(coro));
    EXPECT_EQ(pool.pool_size(), 1u); // returned to pool
}

TEST(CoroutinePoolTest, ReuseReducesAllocations) {
    CoroutinePool pool(8);
    int count = 0;

    for (int i = 0; i < 5; ++i) {
        auto coro = pool.acquire([&] { count++; });
        coro->resume();
        pool.release(std::move(coro));
    }

    EXPECT_EQ(count, 5);
    // After first run, pool should hold 1 reusable coroutine
    EXPECT_EQ(pool.pool_size(), 1u);
}

TEST(CoroutinePoolTest, DoesNotExceedMaxSize) {
    CoroutinePool pool(2 /*max*/);
    std::vector<std::unique_ptr<Coroutine>> coros;

    for (int i = 0; i < 5; ++i) {
        coros.push_back(pool.acquire([] {}));
    }
    for (auto& c : coros) {
        c->resume();
        pool.release(std::move(c));
    }

    EXPECT_LE(pool.pool_size(), 2u);
}
