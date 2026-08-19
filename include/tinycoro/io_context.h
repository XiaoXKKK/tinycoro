#pragma once
#include "tinycoro/coroutine.h"
#include "tinycoro/event_loop.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace tinycoro {

// A single-threaded stackful-coroutine runtime integrated with EventLoop.
// Tasks run cooperatively and are resumed only when their fd becomes ready or
// their deadline expires. One reader and one writer may wait on an fd.
class IoContext {
  public:
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    static constexpr Duration kNoTimeout{-1};

    IoContext() = default;
    ~IoContext();

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

    void spawn(Task task);
    void run();
    void stop();

    // Cooperatively move the current task to the back of the ready queue.
    void yield();

    // Return true for readiness and false for timeout/cancellation.
    bool wait_readable(int fd, Duration timeout = kNoTimeout);
    bool wait_writable(int fd, Duration timeout = kNoTimeout);

    // Remove waiters before an fd is closed. Any suspended tasks are resumed
    // with a false result so they can observe the closed resource.
    void cancel(int fd);

    std::size_t task_count() const { return tasks_.size(); }
    std::size_t ready_count() const { return ready_.size(); }
    bool running() const { return running_; }

  private:
    using TaskId = std::uint64_t;

    enum class TaskState { Ready, Running, Waiting };

    struct TaskRecord {
        std::unique_ptr<Coroutine> coroutine;
        TaskState state{TaskState::Ready};
    };

    struct WaitSlot {
        TaskId task{0};
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
        bool operator()(const TimerEntry& lhs, const TimerEntry& rhs) const {
            return lhs.deadline > rhs.deadline;
        }
    };

    bool wait_fd(int fd, Event event, Duration timeout);
    void run_ready();
    void wake(TaskId task);
    void on_ready(int fd, Event event);
    void complete(std::optional<WaitSlot>& slot, bool result);
    void refresh_interest(FdState& state);
    FdState& fd_state(int fd);
    std::optional<WaitSlot>& slot(FdState& state, Event event);
    const std::optional<WaitSlot>& slot(const FdState& state, Event event) const;
    bool timer_active(const TimerEntry& timer) const;
    void expire_timers();
    int next_poll_timeout();
    bool has_registered_waiter() const;

    EventLoop loop_;
    std::unordered_map<TaskId, TaskRecord> tasks_;
    std::deque<TaskId> ready_;
    std::unordered_map<int, std::unique_ptr<FdState>> fd_states_;
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, TimerLater> timers_;
    TaskId current_task_{0};
    TaskId next_task_{1};
    std::uint64_t next_token_{1};
    bool running_{false};
    bool stop_requested_{false};
};

} // namespace tinycoro