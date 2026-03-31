#include "tinycoro/coroutine.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace tinycoro {

Coroutine::Coroutine(Func fn, std::size_t stack_size)
    : fn_(std::move(fn)),
      stack_size_(stack_size),
      stack_(std::make_unique<char[]>(stack_size)) {
    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_.get();
    ctx_.uc_stack.ss_size = stack_size_;
    ctx_.uc_link = nullptr; // we manage return manually

    // Pass 'this' pointer as two 32-bit halves (portable for 64-bit)
    uintptr_t ptr = reinterpret_cast<uintptr_t>(this);
    uint32_t hi = static_cast<uint32_t>(ptr >> 32);
    uint32_t lo = static_cast<uint32_t>(ptr & 0xFFFFFFFF);
    makecontext(&ctx_, reinterpret_cast<void(*)()>(Coroutine::entry), 2, hi, lo);
}

Coroutine::~Coroutine() = default;

Coroutine::Coroutine(Coroutine&& o) noexcept
    : fn_(std::move(o.fn_)),
      state_(o.state_),
      stack_size_(o.stack_size_),
      stack_(std::move(o.stack_)),
      ctx_(o.ctx_),
      caller_ctx_(o.caller_ctx_) {
    o.state_ = CoroState::DEAD;
}

Coroutine& Coroutine::operator=(Coroutine&& o) noexcept {
    if (this != &o) {
        fn_ = std::move(o.fn_);
        state_ = o.state_;
        stack_size_ = o.stack_size_;
        stack_ = std::move(o.stack_);
        ctx_ = o.ctx_;
        caller_ctx_ = o.caller_ctx_;
        o.state_ = CoroState::DEAD;
    }
    return *this;
}

void Coroutine::resume() {
    assert(state_ == CoroState::READY || state_ == CoroState::SUSPENDED);
    state_ = CoroState::RUNNING;
    swapcontext(&caller_ctx_, &ctx_);
}

void Coroutine::yield() {
    assert(state_ == CoroState::RUNNING);
    state_ = CoroState::SUSPENDED;
    swapcontext(&ctx_, &caller_ctx_);
}

void Coroutine::reset(Func fn) {
    fn_ = std::move(fn);
    state_ = CoroState::READY;
    // Re-initialize context on existing stack
    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_.get();
    ctx_.uc_stack.ss_size = stack_size_;
    ctx_.uc_link = nullptr;
    uintptr_t ptr = reinterpret_cast<uintptr_t>(this);
    uint32_t hi = static_cast<uint32_t>(ptr >> 32);
    uint32_t lo = static_cast<uint32_t>(ptr & 0xFFFFFFFF);
    makecontext(&ctx_, reinterpret_cast<void(*)()>(Coroutine::entry), 2, hi, lo);
}

void Coroutine::entry(uint32_t hi, uint32_t lo) {
    uintptr_t ptr = (static_cast<uintptr_t>(hi) << 32) | static_cast<uintptr_t>(lo);
    Coroutine* self = reinterpret_cast<Coroutine*>(ptr);
    self->fn_();
    self->state_ = CoroState::DEAD;
    // Return to caller context
    swapcontext(&self->ctx_, &self->caller_ctx_);
}

} // namespace tinycoro
