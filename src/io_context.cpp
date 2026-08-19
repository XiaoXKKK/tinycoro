#include "tinycoro/io_context.h"
#include <climits>
#include <stdexcept>
#include <utility>

namespace tinycoro {

IoContext::~IoContext() {
    for (auto& entry : fd_states_) {
        if (entry.second->registered) {
            loop_.remove_channel(&entry.second->channel);
        }
    }
    fd_states_.clear();
    tasks_.clear();
}

void IoContext::spawn(Task task) {
    if (!task)
        throw std::invalid_argument("IoContext::spawn requires a task");
    const TaskId id = next_task_++;
    TaskRecord record;
    record.coroutine = std::make_unique<Coroutine>(std::move(task));
    tasks_.emplace(id, std::move(record));
    ready_.push_back(id);
}

void IoContext::run() {
    if (running_)
        throw std::logic_error("IoContext::run is not reentrant");
    running_ = true;
    stop_requested_ = false;

    try {
        while (!stop_requested_ && !tasks_.empty()) {
            run_ready();
            expire_timers();
            if (stop_requested_ || tasks_.empty())
                break;
            if (!ready_.empty())
                continue;
            const int poll_timeout = next_poll_timeout();
            if (poll_timeout < 0 && !has_registered_waiter()) {
                throw std::logic_error("IoContext task has no wake source");
            }
            loop_.poll(poll_timeout);
            expire_timers();
        }
    } catch (...) {
        current_task_ = 0;
        running_ = false;
        throw;
    }

    current_task_ = 0;
    running_ = false;
}

void IoContext::stop() {
    stop_requested_ = true;
}

void IoContext::yield() {
    if (current_task_ == 0) {
        throw std::logic_error("IoContext::yield called outside a task");
    }
    auto it = tasks_.find(current_task_);
    if (it == tasks_.end() || it->second.state != TaskState::Running) {
        throw std::logic_error("IoContext task is not running");
    }
    it->second.state = TaskState::Ready;
    ready_.push_back(current_task_);
    it->second.coroutine->yield();
}

bool IoContext::wait_readable(int fd, Duration timeout) {
    return wait_fd(fd, Event::READ, timeout);
}

bool IoContext::wait_writable(int fd, Duration timeout) {
    return wait_fd(fd, Event::WRITE, timeout);
}

bool IoContext::wait_fd(int fd, Event event, Duration timeout) {
    if (fd < 0)
        throw std::invalid_argument("cannot wait on an invalid fd");
    if (current_task_ == 0) {
        throw std::logic_error("fd wait called outside an IoContext task");
    }

    auto task_it = tasks_.find(current_task_);
    if (task_it == tasks_.end() || task_it->second.state != TaskState::Running) {
        throw std::logic_error("IoContext task is not running");
    }

    FdState& state = fd_state(fd);
    auto& wait_slot = slot(state, event);
    if (wait_slot) {
        throw std::logic_error("fd already has a waiter for this direction");
    }

    bool ready = false;
    const std::uint64_t token = next_token_++;
    wait_slot = WaitSlot{current_task_, token, &ready};
    refresh_interest(state);

    if (timeout >= Duration::zero()) {
        timers_.push(TimerEntry{Clock::now() + timeout, token, fd, event});
    }

    Coroutine* coroutine = task_it->second.coroutine.get();
    task_it->second.state = TaskState::Waiting;
    coroutine->yield();
    return ready;
}

void IoContext::cancel(int fd) {
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end())
        return;

    FdState& state = *it->second;
    if (state.registered) {
        loop_.remove_channel(&state.channel);
        state.registered = false;
    }
    complete(state.reader, false);
    complete(state.writer, false);
    fd_states_.erase(it);
}

void IoContext::run_ready() {
    while (!ready_.empty() && !stop_requested_) {
        const TaskId id = ready_.front();
        ready_.pop_front();
        auto it = tasks_.find(id);
        if (it == tasks_.end() || it->second.state != TaskState::Ready)
            continue;

        it->second.state = TaskState::Running;
        current_task_ = id;
        try {
            it->second.coroutine->resume();
        } catch (...) {
            current_task_ = 0;
            tasks_.erase(id);
            throw;
        }
        current_task_ = 0;

        it = tasks_.find(id);
        if (it == tasks_.end())
            continue;
        if (it->second.coroutine->is_done()) {
            tasks_.erase(it);
        } else if (it->second.state == TaskState::Running) {
            // A raw Coroutine::yield() is treated as a cooperative yield.
            it->second.state = TaskState::Ready;
            ready_.push_back(id);
        }
    }
}

void IoContext::wake(TaskId task) {
    auto it = tasks_.find(task);
    if (it == tasks_.end() || it->second.state != TaskState::Waiting)
        return;
    it->second.state = TaskState::Ready;
    ready_.push_back(task);
}

void IoContext::on_ready(int fd, Event event) {
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end())
        return;
    FdState& state = *it->second;
    complete(slot(state, event), true);
    refresh_interest(state);
}

void IoContext::complete(std::optional<WaitSlot>& wait_slot, bool result) {
    if (!wait_slot)
        return;
    const WaitSlot completed = *wait_slot;
    wait_slot.reset();
    if (completed.result)
        *completed.result = result;
    wake(completed.task);
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
    auto [it, inserted] = fd_states_.try_emplace(fd);
    if (inserted) {
        it->second = std::make_unique<FdState>();
        it->second->channel.fd = fd;
        it->second->channel.on_read = [this, fd] { on_ready(fd, Event::READ); };
        it->second->channel.on_write = [this, fd] { on_ready(fd, Event::WRITE); };
    }
    return *it->second;
}

std::optional<IoContext::WaitSlot>& IoContext::slot(FdState& state, Event event) {
    return event == Event::READ ? state.reader : state.writer;
}

const std::optional<IoContext::WaitSlot>& IoContext::slot(const FdState& state, Event event) const {
    return event == Event::READ ? state.reader : state.writer;
}

bool IoContext::timer_active(const TimerEntry& timer) const {
    auto it = fd_states_.find(timer.fd);
    if (it == fd_states_.end())
        return false;
    const auto& wait_slot = slot(*it->second, timer.event);
    return wait_slot && wait_slot->token == timer.token;
}

void IoContext::expire_timers() {
    const auto now = Clock::now();
    while (!timers_.empty() && timers_.top().deadline <= now) {
        const TimerEntry timer = timers_.top();
        timers_.pop();
        if (!timer_active(timer))
            continue;
        auto it = fd_states_.find(timer.fd);
        FdState& state = *it->second;
        complete(slot(state, timer.event), false);
        refresh_interest(state);
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
    auto millis = std::chrono::duration_cast<Duration>(remaining);
    if (millis < remaining)
        millis += Duration{1};
    if (millis.count() > INT_MAX)
        return INT_MAX;
    return static_cast<int>(millis.count());
}

bool IoContext::has_registered_waiter() const {
    for (const auto& entry : fd_states_) {
        if (entry.second->registered)
            return true;
    }
    return false;
}

} // namespace tinycoro