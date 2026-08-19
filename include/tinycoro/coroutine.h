#pragma once
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <ucontext.h>

namespace tinycoro {

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

    // ucontext stores this object's address in the entry trampoline.
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine(Coroutine&&) = delete;
    Coroutine& operator=(Coroutine&&) = delete;

    CoroState state() const { return state_; }
    bool is_done() const { return state_ == CoroState::DEAD; }

    void resume();
    void yield();
    void reset(Func fn);

  private:
    static void entry(std::uint32_t hi, std::uint32_t lo);

    Func fn_;
    CoroState state_{CoroState::READY};
    std::size_t stack_size_;
    std::unique_ptr<char[]> stack_;
    ucontext_t ctx_{};
    ucontext_t caller_ctx_{};
    std::exception_ptr exception_{};
};

} // namespace tinycoro