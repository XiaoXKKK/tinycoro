#include "tinycoro/coroutine_pool.h"

namespace tinycoro {

std::unique_ptr<Coroutine> CoroutinePool::acquire(Task fn) {
    std::unique_ptr<Coroutine> coro;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!pool_.empty()) {
            coro = std::move(pool_.back());
            pool_.pop_back();
        }
    }

    if (coro) {
        coro->reset(std::move(fn)); // reuse stack, reset context
    } else {
        coro = std::make_unique<Coroutine>(std::move(fn), stack_size_);
    }
    return coro;
}

void CoroutinePool::release(std::unique_ptr<Coroutine> coro) {
    if (!coro || !coro->is_done()) return;

    std::lock_guard<std::mutex> lk(mu_);
    if (pool_.size() < max_size_) {
        pool_.push_back(std::move(coro));
    }
    // If pool is full, coro goes out of scope and its stack is freed
}

} // namespace tinycoro
