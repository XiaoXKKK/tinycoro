#pragma once
#include "tinycoro/queue.h"
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace tinycoro {

// -----------------------------------------------------------------------
// ThreadPool — fixed number of worker threads pulling tasks from a
// shared MPMC lock-free queue.  No condition variable: workers spin
// briefly then yield the OS thread (avoids syscall on high load).
// -----------------------------------------------------------------------
class ThreadPool {
public:
    using Task = std::function<void()>;
    static constexpr std::size_t kQueueSize = 4096;

    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    // Non-copyable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a task; returns false if the queue is full
    bool submit(Task task);

    std::size_t thread_count() const { return workers_.size(); }

    void shutdown();

private:
    void worker_loop();

    MPMCQueue<Task, kQueueSize> queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
};

} // namespace tinycoro
