#include "tinycoro/coroutine.h"
#include "tinycoro/scheduler.h"
#include <gtest/gtest.h>
#include <vector>

using namespace tinycoro;

// ---- Coroutine basic ---------------------------------------------------

TEST(CoroutineTest, RunsToCompletion) {
    bool ran = false;
    Coroutine coro([&] { ran = true; });
    EXPECT_EQ(coro.state(), CoroState::READY);
    coro.resume();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(coro.is_done());
}

TEST(CoroutineTest, YieldAndResume) {
    std::vector<int> log;
    Coroutine coro([&] {
        log.push_back(1);
        // Access coroutine via closure — yield directly on the object.
        // We test yield by driving it manually (not through scheduler).
    });
    // Simple run-to-completion — yield-from-within tested via Scheduler below.
    coro.resume();
    EXPECT_TRUE(coro.is_done());
}

TEST(CoroutineTest, MultipleYieldsViaScheduler) {
    std::vector<int> order;
    Scheduler sched;

    sched.spawn([&] {
        order.push_back(1);
        Scheduler::yield_current();
        order.push_back(3);
        Scheduler::yield_current();
        order.push_back(5);
    });

    sched.spawn([&] {
        order.push_back(2);
        Scheduler::yield_current();
        order.push_back(4);
    });

    sched.run();

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(CoroutineTest, ResetAndReuse) {
    int count = 0;
    Coroutine coro([&] { count++; });
    coro.resume();
    EXPECT_TRUE(coro.is_done());
    EXPECT_EQ(count, 1);

    coro.reset([&] { count += 10; });
    EXPECT_EQ(coro.state(), CoroState::READY);
    coro.resume();
    EXPECT_EQ(count, 11);
    EXPECT_TRUE(coro.is_done());
}

TEST(CoroutineTest, ManyCoroutines) {
    Scheduler sched;
    std::atomic<int> total{0};
    for (int i = 0; i < 1000; ++i) {
        sched.spawn([&total] {
            total.fetch_add(1, std::memory_order_relaxed);
        });
    }
    sched.run();
    EXPECT_EQ(total.load(), 1000);
}
