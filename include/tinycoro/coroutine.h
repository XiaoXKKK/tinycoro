#pragma once
#include <functional>
#include <memory>
#include <ucontext.h>

namespace tinycoro {

// Stack size for each coroutine (128 KB default)
static constexpr std::size_t kDefaultStackSize = 128 * 1024;

enum class CoroState {
    READY,
    RUNNING,
    SUSPENDED,
    DEAD,
};

class Coroutine {
public:
    using Func = std::function<void()>;

    explicit Coroutine(Func fn, std::size_t stack_size = kDefaultStackSize);
    ~Coroutine();

    // Non-copyable, movable
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine(Coroutine&&) noexcept;
    Coroutine& operator=(Coroutine&&) noexcept;

    CoroState state() const { return state_; }
    bool is_done() const { return state_ == CoroState::DEAD; }

    // Resume this coroutine from caller context
    void resume();

    // Yield back to caller; must be called from within the coroutine
    void yield();

    // Reset with a new function (for pool reuse)
    void reset(Func fn);

private:
    static void entry(uint32_t hi, uint32_t lo);

    Func fn_;
    CoroState state_{CoroState::READY};
    std::size_t stack_size_;
    std::unique_ptr<char[]> stack_;
    ucontext_t ctx_{};
    ucontext_t caller_ctx_{};
};

} // namespace tinycoro
