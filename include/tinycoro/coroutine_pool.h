#pragma once
#include "tinycoro/coroutine.h"
#include <memory>
#include <mutex>
#include <vector>

namespace tinycoro {

// -----------------------------------------------------------------------
// CoroutinePool — reuses Coroutine objects (and their stacks) to amortize
// the cost of mmap/stack allocation on hot paths.
//
// Each released coroutine is reset() with the next task rather than
// being destroyed and re-created.  The pool is bounded by max_size to
// cap memory usage.
// -----------------------------------------------------------------------
class CoroutinePool {
public:
    using Task = Coroutine::Func;

    explicit CoroutinePool(std::size_t max_size = 256,
                           std::size_t stack_size = kDefaultStackSize)
        : max_size_(max_size), stack_size_(stack_size) {}

    // Acquire a coroutine loaded with 'fn'; creates one if pool is empty
    std::unique_ptr<Coroutine> acquire(Task fn);

    // Return a finished coroutine back to the pool (ignored if dead check fails)
    void release(std::unique_ptr<Coroutine> coro);

    std::size_t pool_size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return pool_.size();
    }

private:
    std::size_t max_size_;
    std::size_t stack_size_;
    mutable std::mutex mu_;
    std::vector<std::unique_ptr<Coroutine>> pool_;
};

} // namespace tinycoro
