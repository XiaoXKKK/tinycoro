#include "tinycoro/scheduler.h"
#include <cassert>

namespace tinycoro {

thread_local Scheduler* Scheduler::tl_current_scheduler_ = nullptr;
thread_local Coroutine* Scheduler::tl_current_coro_ = nullptr;

void Scheduler::spawn(Task fn) {
    ready_queue_.push(std::make_unique<Coroutine>(std::move(fn)));
}

void Scheduler::run() {
    Scheduler* prev = tl_current_scheduler_;
    tl_current_scheduler_ = this;

    while (!ready_queue_.empty()) {
        running_ = std::move(ready_queue_.front());
        ready_queue_.pop();

        tl_current_coro_ = running_.get();
        running_->resume();
        tl_current_coro_ = nullptr;

        // If coroutine yielded (not dead), re-enqueue
        if (!running_->is_done()) {
            ready_queue_.push(std::move(running_));
        }
        running_.reset();
    }

    tl_current_scheduler_ = prev;
}

void Scheduler::yield_current() {
    assert(tl_current_coro_ != nullptr && "yield_current called outside a coroutine");
    tl_current_coro_->yield();
}

Coroutine* Scheduler::current() {
    return tl_current_coro_;
}

} // namespace tinycoro
