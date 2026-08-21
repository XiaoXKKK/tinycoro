#include "tinycoro/thread_pool.h"
#include <stdexcept>
#include <utility>

namespace tinycoro {

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0)
        throw std::invalid_argument("ThreadPool requires workers");

    workers_.reserve(num_threads);
    try {
        for (std::size_t index = 0; index < num_threads; ++index) {
            (void)index;
            workers_.emplace_back(
                [this](std::stop_token stop_token) { worker_loop(stop_token); });
        }
    } catch (...) {
        accepting_.store(false, std::memory_order_release);
        for (auto& worker : workers_)
            worker.request_stop();
        available_.release(static_cast<std::ptrdiff_t>(workers_.size()));
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::submit(Task task) {
    if (!task)
        return false;

    std::lock_guard lock(lifecycle_mutex_);
    if (!accepting_.load(std::memory_order_acquire))
        return false;
    if (!queue_.push(std::move(task)))
        return false;
    available_.release();
    return true;
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (accepting_.exchange(false, std::memory_order_acq_rel)) {
            for (auto& worker : workers_)
                worker.request_stop();
            available_.release(static_cast<std::ptrdiff_t>(workers_.size()));
        }
    }

    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
    workers_.clear();
}

std::vector<std::exception_ptr> ThreadPool::take_errors() {
    std::lock_guard lock(errors_mutex_);
    return std::exchange(errors_, {});
}

void ThreadPool::worker_loop(std::stop_token stop_token) {
    for (;;) {
        available_.acquire();

        Task task;
        if (queue_.pop(task)) {
            try {
                task();
            } catch (...) {
                std::lock_guard lock(errors_mutex_);
                errors_.push_back(std::current_exception());
            }
            continue;
        }

        if (stop_token.stop_requested())
            return;
    }
}

} // namespace tinycoro
