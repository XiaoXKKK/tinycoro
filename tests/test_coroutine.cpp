#include "tinycoro/coroutine.h"
#include "tinycoro/scheduler.h"
#include <atomic>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

using namespace tinycoro;

TEST(CoroutineTest, RunsToCompletion) {
    bool ran = false;
    Coroutine coroutine([&] { ran = true; });
    EXPECT_EQ(coroutine.state(), CoroState::READY);
    coroutine.resume();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(coroutine.is_done());
}

TEST(CoroutineTest, MultipleYieldsViaScheduler) {
    std::vector<int> order;
    Scheduler scheduler;

    scheduler.spawn([&] {
        order.push_back(1);
        Scheduler::yield_current();
        order.push_back(3);
        Scheduler::yield_current();
        order.push_back(5);
    });
    scheduler.spawn([&] {
        order.push_back(2);
        Scheduler::yield_current();
        order.push_back(4);
    });

    scheduler.run();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(CoroutineTest, ResetAndReuse) {
    int count = 0;
    Coroutine coroutine([&] { ++count; });
    coroutine.resume();
    EXPECT_TRUE(coroutine.is_done());

    coroutine.reset([&] { count += 10; });
    EXPECT_EQ(coroutine.state(), CoroState::READY);
    coroutine.resume();
    EXPECT_EQ(count, 11);
    EXPECT_TRUE(coroutine.is_done());
}

TEST(CoroutineTest, ManyCoroutines) {
    Scheduler scheduler;
    std::atomic<int> total{0};
    for (int i = 0; i < 1000; ++i) {
        scheduler.spawn([&total] { total.fetch_add(1, std::memory_order_relaxed); });
    }
    scheduler.run();
    EXPECT_EQ(total.load(), 1000);
}

TEST(CoroutineTest, ExceptionsCrossTheContextBoundary) {
    Coroutine coroutine([] { throw std::runtime_error("coroutine failure"); });
    EXPECT_THROW(coroutine.resume(), std::runtime_error);
    EXPECT_TRUE(coroutine.is_done());
}