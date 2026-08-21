#pragma once

#include <coroutine>
#include <concepts>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tinycoro {

class IoContext;
template <typename T = void> class Task;

namespace detail {

struct TaskPromiseBase {
    using Completion = void (*)(void*, std::coroutine_handle<>) noexcept;

    std::coroutine_handle<> continuation{std::noop_coroutine()};
    std::coroutine_handle<> completed_next{};
    std::exception_ptr exception;
    void* completion_context{nullptr};
    Completion completion{nullptr};

    std::suspend_always initial_suspend() const noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> current) const
            noexcept {
            auto& promise = current.promise();
            if (promise.completion) {
                const auto handle = std::coroutine_handle<>::from_address(current.address());
                promise.completion(promise.completion_context, handle);
                return std::noop_coroutine();
            }
            return promise.continuation ? promise.continuation : std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    FinalAwaiter final_suspend() const noexcept { return {}; }
    void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <typename T> struct TaskPromise final : TaskPromiseBase {
    std::optional<T> value;

    Task<T> get_return_object() noexcept;

    template <typename U>
        requires std::constructible_from<T, U&&>
    void return_value(U&& result) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        value.emplace(std::forward<U>(result));
    }
};

template <> struct TaskPromise<void> final : TaskPromiseBase {
    Task<void> get_return_object() noexcept;
    void return_void() const noexcept {}
};

} // namespace detail

template <typename T> class [[nodiscard]] Task {
  public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this == &other)
            return *this;
        reset();
        handle_ = std::exchange(other.handle_, {});
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() { reset(); }

    bool valid() const noexcept { return static_cast<bool>(handle_); }

    class Awaiter {
      public:
        explicit Awaiter(handle_type handle) noexcept : handle_(handle) {}

        Awaiter(Awaiter&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(const Awaiter&) = delete;
        Awaiter& operator=(Awaiter&&) = delete;

        ~Awaiter() {
            if (handle_)
                handle_.destroy();
        }

        bool await_ready() const noexcept { return !handle_ || handle_.done(); }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            handle_.promise().continuation = continuation;
            return handle_;
        }

        T await_resume()
            requires(!std::is_void_v<T>)
        {
            ensure_valid();
            auto& promise = handle_.promise();
            if (promise.exception)
                std::rethrow_exception(promise.exception);
            if (!promise.value)
                throw std::logic_error("Task completed without a value");
            return std::move(*promise.value);
        }

        void await_resume()
            requires std::is_void_v<T>
        {
            ensure_valid();
            if (handle_.promise().exception)
                std::rethrow_exception(handle_.promise().exception);
        }

      private:
        void ensure_valid() const {
            if (!handle_)
                throw std::logic_error("cannot await an empty Task");
        }

        handle_type handle_{};
    };

    Awaiter operator co_await() && noexcept {
        return Awaiter{std::exchange(handle_, {})};
    }

    Awaiter operator co_await() & = delete;

  private:
    explicit Task(handle_type handle) noexcept : handle_(handle) {}

    handle_type release() noexcept { return std::exchange(handle_, {}); }

    void reset() noexcept {
        if (handle_)
            handle_.destroy();
        handle_ = {};
    }

    handle_type handle_{};

    friend class IoContext;
    friend struct detail::TaskPromise<T>;
};

namespace detail {

template <typename T> Task<T> TaskPromise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
}

} // namespace detail

} // namespace tinycoro
