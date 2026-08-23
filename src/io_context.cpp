#include "tinycoro/io_context.h"
#include <climits>
#include <exception>
#include <stdexcept>
#include <utility>

namespace tinycoro {

void IoContext::FdAwaiter::await_suspend(std::coroutine_handle<> continuation) {
    context_->arm_wait(fd_, event_, timeout_, continuation, &ready_);
}

void IoContext::YieldAwaiter::await_suspend(std::coroutine_handle<> continuation) {
    context_->schedule(continuation);
}

IoContext::~IoContext() {
    for (auto& [fd, state] : fd_states_) {
        (void)fd;
        if (state->registered)
            loop_.remove_channel(&state->channel);
    }
    fd_states_.clear();
    ready_.clear();
    completed_head_ = {};
    timers_ = {};

    auto roots = std::move(roots_);
    roots_.clear();
    for (auto& [address, handle] : roots) {
        (void)address;
        if (handle)
            handle.destroy();
    }
}

void IoContext::spawn(Task<void> task) {
    if (!task.valid())
        throw std::invalid_argument("IoContext::spawn requires a valid Task");

    RootHandle handle = task.release();
    auto& promise = handle.promise();
    promise.completion_context = this;
    promise.completion = &IoContext::notify_root_completed;

    try {
        const auto [iterator, inserted] = roots_.emplace(handle.address(), handle);
        (void)iterator;
        if (!inserted)
            throw std::logic_error("IoContext already owns this Task");
        ready_.push_back(handle);
    } catch (...) {
        roots_.erase(handle.address());
        promise.completion_context = nullptr;
        promise.completion = nullptr;
        handle.destroy();
        throw;
    }
}

void IoContext::run() {
    if (running_)
        throw std::logic_error("IoContext::run is not reentrant");
    running_ = true;
    stop_requested_ = false;

    try {
        while (!stop_requested_ && !roots_.empty()) {
            run_ready();
            expire_timers();
            if (stop_requested_ || roots_.empty())
                break;
            const bool has_ready = !ready_.empty();
            const int poll_timeout = has_ready ? 0 : next_poll_timeout();
            if (!has_ready && poll_timeout < 0 && !has_registered_waiter()) {
                throw std::logic_error("IoContext Task has no readiness or timer wake source");
            }
            loop_.poll(poll_timeout);
            expire_timers();
        }
    } catch (...) {
        running_ = false;
        throw;
    }

    running_ = false;
}

void IoContext::stop() noexcept {
    stop_requested_ = true;
}

void IoContext::cancel(int fd) {
    auto iterator = fd_states_.find(fd);
    if (iterator == fd_states_.end())
        return;

    FdState& state = *iterator->second;
    if (state.registered) {
        loop_.remove_channel(&state.channel);
        state.registered = false;
    }
    complete(state.reader, false);
    complete(state.writer, false);
    fd_states_.erase(iterator);
}

void IoContext::notify_root_completed(void* context, std::coroutine_handle<> handle) noexcept {
    static_cast<IoContext*>(context)->root_completed(handle);
}

void IoContext::root_completed(std::coroutine_handle<> handle) noexcept {
    RootHandle root = RootHandle::from_address(handle.address());
    root.promise().completed_next = completed_head_;
    completed_head_ = handle;
}

void IoContext::drain_completed() {
    std::exception_ptr first_error;
    while (completed_head_) {
        RootHandle root = RootHandle::from_address(completed_head_.address());
        completed_head_ = root.promise().completed_next;
        roots_.erase(root.address());
        if (!first_error && root.promise().exception)
            first_error = root.promise().exception;
        root.destroy();
    }
    if (first_error)
        std::rethrow_exception(first_error);
}

void IoContext::schedule(std::coroutine_handle<> continuation) {
    if (!continuation || continuation.done())
        return;
    ready_.push_back(continuation);
}

void IoContext::run_ready() {
    // Bound each scheduler turn so a task that repeatedly yields cannot keep
    // fd readiness and deadline processing from running indefinitely.
    const std::size_t batch = ready_.size();
    for (std::size_t index = 0; index < batch && !stop_requested_; ++index) {
        const std::coroutine_handle<> continuation = ready_.front();
        ready_.pop_front();
        if (!continuation || continuation.done())
            continue;
        continuation.resume();
        drain_completed();
    }
}

void IoContext::arm_wait(int fd, Event event, Duration timeout,
                         std::coroutine_handle<> continuation, bool* result) {
    if (fd < 0)
        throw std::invalid_argument("cannot wait on an invalid fd");
    if (!continuation || continuation.done())
        throw std::logic_error("cannot register an invalid coroutine continuation");
    if (!result)
        throw std::invalid_argument("fd wait requires result storage");

    FdState& state = fd_state(fd);
    auto& wait_slot = slot(state, event);
    if (wait_slot)
        throw std::logic_error("fd already has a waiter for this direction");

    *result = false;
    const std::uint64_t token = next_token_++;
    wait_slot = WaitSlot{continuation, token, result};

    try {
        refresh_interest(state);
        if (timeout >= Duration::zero())
            timers_.push(TimerEntry{Clock::now() + timeout, token, fd, event});
    } catch (...) {
        wait_slot.reset();
        if (!state.reader && !state.writer) {
            if (state.registered)
                loop_.remove_channel(&state.channel);
            fd_states_.erase(fd);
        } else {
            try {
                refresh_interest(state);
            } catch (...) {
            }
        }
        throw;
    }
}

void IoContext::on_ready(int fd, Event event) {
    auto iterator = fd_states_.find(fd);
    if (iterator == fd_states_.end())
        return;

    FdState& state = *iterator->second;
    complete(slot(state, event), true);
    refresh_interest(state);
    if (!state.reader && !state.writer)
        fd_states_.erase(iterator);
}

void IoContext::complete(std::optional<WaitSlot>& wait_slot, bool result) {
    if (!wait_slot)
        return;
    const WaitSlot completed = *wait_slot;
    wait_slot.reset();
    *completed.result = result;
    schedule(completed.continuation);
}

void IoContext::refresh_interest(FdState& state) {
    if (!state.reader && !state.writer) {
        if (state.registered) {
            loop_.remove_channel(&state.channel);
            state.registered = false;
        }
        return;
    }

    if (state.reader && state.writer) {
        state.channel.interest = Event::READ | Event::WRITE;
    } else if (state.reader) {
        state.channel.interest = Event::READ;
    } else {
        state.channel.interest = Event::WRITE;
    }

    if (state.registered) {
        loop_.update_channel(&state.channel);
    } else {
        loop_.add_channel(&state.channel);
        state.registered = true;
    }
}

IoContext::FdState& IoContext::fd_state(int fd) {
    const auto existing = fd_states_.find(fd);
    if (existing != fd_states_.end())
        return *existing->second;

    auto state = std::make_unique<FdState>();
    state->channel.fd = fd;
    state->channel.on_read = [this, fd] { on_ready(fd, Event::READ); };
    state->channel.on_write = [this, fd] { on_ready(fd, Event::WRITE); };

    auto [iterator, inserted] = fd_states_.emplace(fd, std::move(state));
    (void)inserted;
    return *iterator->second;
}

std::optional<IoContext::WaitSlot>& IoContext::slot(FdState& state, Event event) {
    return event == Event::READ ? state.reader : state.writer;
}

const std::optional<IoContext::WaitSlot>& IoContext::slot(const FdState& state, Event event) const {
    return event == Event::READ ? state.reader : state.writer;
}

bool IoContext::timer_active(const TimerEntry& timer) const {
    const auto iterator = fd_states_.find(timer.fd);
    if (iterator == fd_states_.end())
        return false;
    const auto& wait_slot = slot(*iterator->second, timer.event);
    return wait_slot && wait_slot->token == timer.token;
}

void IoContext::expire_timers() {
    const auto now = Clock::now();
    while (!timers_.empty() && timers_.top().deadline <= now) {
        const TimerEntry timer = timers_.top();
        timers_.pop();
        if (!timer_active(timer))
            continue;

        auto iterator = fd_states_.find(timer.fd);
        FdState& state = *iterator->second;
        complete(slot(state, timer.event), false);
        refresh_interest(state);
        if (!state.reader && !state.writer)
            fd_states_.erase(iterator);
    }
}

int IoContext::next_poll_timeout() {
    while (!timers_.empty() && !timer_active(timers_.top()))
        timers_.pop();
    if (timers_.empty())
        return -1;

    const auto now = Clock::now();
    if (timers_.top().deadline <= now)
        return 0;
    const auto remaining = timers_.top().deadline - now;
    auto milliseconds = std::chrono::duration_cast<Duration>(remaining);
    if (milliseconds < remaining)
        milliseconds += Duration{1};
    if (milliseconds.count() > INT_MAX)
        return INT_MAX;
    return static_cast<int>(milliseconds.count());
}

bool IoContext::has_registered_waiter() const {
    for (const auto& [fd, state] : fd_states_) {
        (void)fd;
        if (state->registered)
            return true;
    }
    return false;
}

} // namespace tinycoro
