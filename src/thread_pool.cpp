#include "tinycoro/thread_pool.h"
#include <stdexcept>

namespace tinycoro {

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0)
        throw std::invalid_argument("ThreadPool requires workers");
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::submit(Task task) {
    if (!task || stop_.load(std::memory_order_acquire))
        return false;
    return queue_.push(std::move(task));
}

void ThreadPool::shutdown() {
    stop_.store(true, std::memory_order_release);
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
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
        } else if (++spin > kSpinLimit) {
            std::this_thread::yield();
            spin = 0;
        }
    }

    Task task;
    while (queue_.pop(task))
        task();
}

} // namespace tinycoro