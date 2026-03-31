#include "tinycoro/thread_pool.h"

namespace tinycoro {

ThreadPool::ThreadPool(std::size_t num_threads) {
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::submit(Task task) {
    return queue_.push(std::move(task));
}

void ThreadPool::shutdown() {
    stop_.store(true, std::memory_order_release);
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void ThreadPool::worker_loop() {
    int spin = 0;
    static constexpr int kSpinLimit = 100;

    while (!stop_.load(std::memory_order_acquire)) {
        Task task;
        if (queue_.pop(task)) {
            task();
            spin = 0;
        } else {
            // Spin briefly before yielding to avoid unnecessary context switches
            // under transient low-load conditions
            if (++spin > kSpinLimit) {
                std::this_thread::yield();
                spin = 0;
            }
        }
    }

    // Drain remaining tasks after stop signal
    Task task;
    while (queue_.pop(task)) {
        task();
    }
}

} // namespace tinycoro
