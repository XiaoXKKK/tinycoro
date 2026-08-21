#pragma once

#include "tinycoro/event_loop.h"
#include "tinycoro/task.h"
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace tinycoro {

// A single-threaded C++20 coroutine executor integrated with Linux epoll.
// Root Task<void> frames are owned by IoContext; nested Task<T> frames are
// owned by their awaiting coroutine. An fd may have one reader and one writer.
class IoContext {
  public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    static constexpr Duration kNoTimeout{-1};

    class FdAwaiter {
      public:
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> continuation);
        bool await_resume() const noexcept { return ready_; }

      private:
        FdAwaiter(IoContext& context, int fd, Event event, Duration timeout) noexcept
            : context_(&context), fd_(fd), event_(event), timeout_(timeout) {}

        IoContext* context_;
        int fd_;
        Event event_;
        Duration timeout_;
        bool ready_{false};

        friend class IoContext;
    };

    class YieldAwaiter {
      public:
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> continuation);
        void await_resume() const noexcept {}

      private:
        explicit YieldAwaiter(IoContext& context) noexcept : context_(&context) {}

        IoContext* context_;

        friend class IoContext;
    };

    IoContext() = default;
    ~IoContext();

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

    // Transfers ownership of a lazy root task to this executor.
    void spawn(Task<void> task);
    void run();
    void stop() noexcept;

    YieldAwaiter yield() noexcept { return YieldAwaiter{*this}; }
    FdAwaiter wait_readable(int fd, Duration timeout = kNoTimeout) noexcept {
        return FdAwaiter{*this, fd, Event::READ, timeout};
    }
    FdAwaiter wait_writable(int fd, Duration timeout = kNoTimeout) noexcept {
        return FdAwaiter{*this, fd, Event::WRITE, timeout};
    }

    // Remove waiters before an fd is closed. Suspended coroutines are scheduled
    // with a false result so their owning operation can report cancellation.
    void cancel(int fd);

    std::size_t task_count() const noexcept { return roots_.size(); }
    std::size_t ready_count() const noexcept { return ready_.size(); }
    bool running() const noexcept { return running_; }

  private:
    using RootHandle = Task<void>::handle_type;

    struct WaitSlot {
        std::coroutine_handle<> continuation{};
        std::uint64_t token{0};
        bool* result{nullptr};
    };

    struct FdState {
        Channel channel;
        std::optional<WaitSlot> reader;
        std::optional<WaitSlot> writer;
        bool registered{false};
    };

    struct TimerEntry {
        Clock::time_point deadline;
        std::uint64_t token{0};
        int fd{-1};
        Event event{Event::READ};
    };

    struct TimerLater {
        bool operator()(const TimerEntry& lhs, const TimerEntry& rhs) const noexcept {
            return lhs.deadline > rhs.deadline;
        }
    };

    static void notify_root_completed(void* context, std::coroutine_handle<> handle) noexcept;
    void root_completed(std::coroutine_handle<> handle) noexcept;
    void drain_completed();

    void schedule(std::coroutine_handle<> continuation);
    void run_ready();
    void arm_wait(int fd, Event event, Duration timeout, std::coroutine_handle<> continuation,
                  bool* result);
    void on_ready(int fd, Event event);
    void complete(std::optional<WaitSlot>& wait_slot, bool result);
    void refresh_interest(FdState& state);
    FdState& fd_state(int fd);
    std::optional<WaitSlot>& slot(FdState& state, Event event);
    const std::optional<WaitSlot>& slot(const FdState& state, Event event) const;
    bool timer_active(const TimerEntry& timer) const;
    void expire_timers();
    int next_poll_timeout();
    bool has_registered_waiter() const;

    EventLoop loop_;
    std::unordered_map<void*, RootHandle> roots_;
    std::deque<std::coroutine_handle<>> ready_;
    std::coroutine_handle<> completed_head_{};
    std::unordered_map<int, std::unique_ptr<FdState>> fd_states_;
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, TimerLater> timers_;
    std::uint64_t next_token_{1};
    bool running_{false};
    bool stop_requested_{false};
};

} // namespace tinycoro
