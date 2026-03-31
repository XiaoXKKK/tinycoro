#pragma once
#include "tinycoro/coroutine.h"
#include <functional>
#include <memory>
#include <vector>
#include <queue>

namespace tinycoro {

// Single-thread cooperative scheduler.
// Run-to-yield semantics: each spawned coroutine runs until it yields or finishes.
class Scheduler {
public:
    using Task = std::function<void()>;

    Scheduler() = default;
    ~Scheduler() = default;

    // Spawn a new coroutine task
    void spawn(Task fn);

    // Run all ready coroutines until none remain
    void run();

    // Yield the currently running coroutine back to the scheduler
    // Must be called from within a scheduled coroutine
    static void yield_current();

    // Returns the currently running coroutine (nullptr if called from main thread)
    static Coroutine* current();

private:
    void schedule_next();

    std::queue<std::unique_ptr<Coroutine>> ready_queue_;
    std::unique_ptr<Coroutine> running_;

    // Thread-local pointer to the scheduler running on this thread
    static thread_local Scheduler* tl_current_scheduler_;
    static thread_local Coroutine* tl_current_coro_;
};

} // namespace tinycoro
