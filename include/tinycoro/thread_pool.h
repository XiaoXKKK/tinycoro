#pragma once

#include "tinycoro/queue.h"
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <semaphore>
#include <stop_token>
#include <thread>
#include <vector>

namespace tinycoro {

// A bounded general-purpose pool. Workers block on a C++20 semaphore when idle;
// an MPMC queue transfers tasks to multiple consumers. Submission is serialized
// with shutdown by a lifecycle mutex, so the pool itself is not lock-free. This
// pool is independent of IoContext, whose fd state and coroutine frames remain
// on one executor thread.
class ThreadPool {
  public:
    using Task = std::function<void()>;
    static constexpr std::size_t kQueueSize = 4096;

    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Non-blocking submission: false means shutdown has started or the bounded
    // queue is full. A successful task is always drained before shutdown returns.
    bool submit(Task task);

    std::size_t thread_count() const noexcept { return workers_.size(); }
    void shutdown();

    // Worker exceptions are captured rather than terminating the process.
    // Ownership of the accumulated exceptions transfers to the caller.
    std::vector<std::exception_ptr> take_errors();

  private:
    void worker_loop(std::stop_token stop_token);

    MPMCQueue<Task, kQueueSize> queue_;
    std::vector<std::jthread> workers_;
    std::counting_semaphore<> available_{0};
    std::atomic<bool> accepting_{true};
    std::mutex lifecycle_mutex_;
    std::mutex errors_mutex_;
    std::vector<std::exception_ptr> errors_;
};

} // namespace tinycoro
