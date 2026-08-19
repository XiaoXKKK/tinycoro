#include "tinycoro/coroutine.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <system_error>
#include <utility>

namespace tinycoro {

Coroutine::Coroutine(Func fn, std::size_t stack_size)
    : fn_(std::move(fn)), stack_size_(stack_size), stack_(std::make_unique<char[]>(stack_size)) {
    if (getcontext(&ctx_) < 0) {
        throw std::system_error(errno, std::generic_category(), "getcontext");
    }
    ctx_.uc_stack.ss_sp = stack_.get();
    ctx_.uc_stack.ss_size = stack_size_;
    ctx_.uc_link = nullptr;

    const auto ptr = reinterpret_cast<std::uintptr_t>(this);
    const auto hi = static_cast<std::uint32_t>(ptr >> 32U);
    const auto lo = static_cast<std::uint32_t>(ptr & 0xFFFFFFFFU);
    makecontext(&ctx_, reinterpret_cast<void (*)()>(Coroutine::entry), 2, hi, lo);
}

Coroutine::~Coroutine() = default;

void Coroutine::resume() {
    assert(state_ == CoroState::READY || state_ == CoroState::SUSPENDED);
    const CoroState previous = state_;
    state_ = CoroState::RUNNING;
    if (swapcontext(&caller_ctx_, &ctx_) < 0) {
        state_ = previous;
        throw std::system_error(errno, std::generic_category(), "swapcontext resume");
    }
    if (state_ == CoroState::DEAD && exception_) {
        auto error = std::exchange(exception_, {});
        std::rethrow_exception(error);
    }
}

void Coroutine::yield() {
    assert(state_ == CoroState::RUNNING);
    state_ = CoroState::SUSPENDED;
    if (swapcontext(&ctx_, &caller_ctx_) < 0) {
        state_ = CoroState::RUNNING;
        throw std::system_error(errno, std::generic_category(), "swapcontext yield");
    }
}

void Coroutine::reset(Func fn) {
    fn_ = std::move(fn);
    state_ = CoroState::READY;
    exception_ = {};
    if (getcontext(&ctx_) < 0) {
        throw std::system_error(errno, std::generic_category(), "getcontext reset");
    }
    ctx_.uc_stack.ss_sp = stack_.get();
    ctx_.uc_stack.ss_size = stack_size_;
    ctx_.uc_link = nullptr;
    const auto ptr = reinterpret_cast<std::uintptr_t>(this);
    const auto hi = static_cast<std::uint32_t>(ptr >> 32U);
    const auto lo = static_cast<std::uint32_t>(ptr & 0xFFFFFFFFU);
    makecontext(&ctx_, reinterpret_cast<void (*)()>(Coroutine::entry), 2, hi, lo);
}

void Coroutine::entry(std::uint32_t hi, std::uint32_t lo) {
    const auto ptr = (static_cast<std::uintptr_t>(hi) << 32U) | static_cast<std::uintptr_t>(lo);
    auto* self = reinterpret_cast<Coroutine*>(ptr);
    try {
        self->fn_();
    } catch (...) {
        self->exception_ = std::current_exception();
    }
    self->state_ = CoroState::DEAD;
    if (swapcontext(&self->ctx_, &self->caller_ctx_) < 0)
        std::terminate();
}

} // namespace tinycoro