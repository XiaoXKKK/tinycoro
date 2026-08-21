#include "tinycoro/io_context.h"
#include "tinycoro/task.h"
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace tinycoro;

namespace {

Task<void> set_flag(bool& flag) {
    flag = true;
    co_return;
}

Task<int> answer() {
    co_return 41;
}

Task<int> add_one() {
    co_return (co_await answer()) + 1;
}

Task<void> store_nested_result(int& result) {
    result = co_await add_one();
}

Task<void> append_with_yield(IoContext& context, std::vector<int>& order, int before, int after) {
    order.push_back(before);
    co_await context.yield();
    order.push_back(after);
}

Task<void> fail_after_yield(IoContext& context) {
    co_await context.yield();
    throw std::runtime_error("task failure");
}

Task<int> fail_in_child() {
    throw std::runtime_error("nested task failure");
    co_return 0;
}

Task<void> await_failing_child() {
    (void)co_await fail_in_child();
}

Task<void> await_empty_task() {
    Task<void> empty;
    co_await std::move(empty);
}

struct LifetimeProbe {
    explicit LifetimeProbe(bool& destroyed) : destroyed_(&destroyed) {}
    ~LifetimeProbe() { *destroyed_ = true; }

    bool* destroyed_;
};

Task<void> wait_forever(IoContext& context, int fd, bool& destroyed) {
    LifetimeProbe probe(destroyed);
    (void)co_await context.wait_readable(fd);
}

Task<void> stop_after_yield(IoContext& context) {
    co_await context.yield();
    context.stop();
}

} // namespace

TEST(TaskTest, IsLazyUntilSpawned) {
    bool ran = false;
    auto task = set_flag(ran);
    EXPECT_FALSE(ran);

    IoContext context;
    context.spawn(std::move(task));
    context.run();

    EXPECT_TRUE(ran);
    EXPECT_EQ(context.task_count(), 0u);
}

TEST(TaskTest, NestedTaskReturnsValue) {
    int result = 0;
    IoContext context;
    context.spawn(store_nested_result(result));
    context.run();
    EXPECT_EQ(result, 42);
}

TEST(TaskTest, YieldSchedulesReadyTasksInFifoOrder) {
    std::vector<int> order;
    IoContext context;
    context.spawn(append_with_yield(context, order, 1, 3));
    context.spawn(append_with_yield(context, order, 2, 4));
    context.run();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
}

TEST(TaskTest, ExceptionPropagatesToRunCaller) {
    IoContext context;
    context.spawn(fail_after_yield(context));
    EXPECT_THROW(context.run(), std::runtime_error);
    EXPECT_EQ(context.task_count(), 0u);
}

TEST(TaskTest, NestedExceptionPropagatesThroughContinuation) {
    IoContext context;
    context.spawn(await_failing_child());
    EXPECT_THROW(context.run(), std::runtime_error);
    EXPECT_EQ(context.task_count(), 0u);
}

TEST(TaskTest, AwaitingEmptyTaskFailsAtAwaitBoundary) {
    IoContext context;
    context.spawn(await_empty_task());
    EXPECT_THROW(context.run(), std::logic_error);
    EXPECT_EQ(context.task_count(), 0u);
}

TEST(TaskTest, ContextDestructionDestroysSuspendedFrames) {
    int sockets[2]{-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    bool destroyed = false;

    {
        IoContext context;
        context.spawn(wait_forever(context, sockets[0], destroyed));
        context.spawn(stop_after_yield(context));
        context.run();
        EXPECT_FALSE(destroyed);
        EXPECT_EQ(context.task_count(), 1u);
    }

    EXPECT_TRUE(destroyed);
    ::close(sockets[0]);
    ::close(sockets[1]);
}
