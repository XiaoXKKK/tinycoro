#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>

namespace tinycoro {

// -----------------------------------------------------------------------
// SPSCQueue — single-producer / single-consumer lock-free ring queue.
// Uses relaxed + acquire/release ordering; no CAS needed.
// -----------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Called by producer thread only
    bool push(const T& val) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[tail] = val;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& val) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[tail] = std::move(val);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Called by consumer thread only
    std::optional<T> pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }
        T val = std::move(buffer_[head]);
        head_.store((head + 1) & mask_, std::memory_order_release);
        return val;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    std::size_t size() const {
        std::size_t h = head_.load(std::memory_order_acquire);
        std::size_t t = tail_.load(std::memory_order_acquire);
        return (t - h + Capacity) & mask_;
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    // Pad to separate cache lines and avoid false sharing
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
    T buffer_[Capacity];
};


// -----------------------------------------------------------------------
// MPMCQueue — multi-producer / multi-consumer lock-free ring queue.
// Based on Dmitry Vyukov's classic design: each slot has its own
// sequence counter so readers and writers can proceed independently.
// -----------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    struct Slot {
        alignas(64) std::atomic<std::size_t> sequence;
        T data;
    };

public:
    MPMCQueue() : enqueue_pos_(0), dequeue_pos_(0) {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool push(T val) {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = slots_[pos & mask_];
            std::size_t seq = slot.sequence.load(std::memory_order_acquire);
            std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                  static_cast<std::ptrdiff_t>(pos);
            if (diff == 0) {
                // Slot is free; try to claim it
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                                       std::memory_order_relaxed)) {
                    slot.data = std::move(val);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // queue full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(T& val) {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = slots_[pos & mask_];
            std::size_t seq = slot.sequence.load(std::memory_order_acquire);
            std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                  static_cast<std::ptrdiff_t>(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                                       std::memory_order_relaxed)) {
                    val = std::move(slot.data);
                    slot.sequence.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // queue empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    bool empty() const {
        std::size_t ep = enqueue_pos_.load(std::memory_order_acquire);
        std::size_t dp = dequeue_pos_.load(std::memory_order_acquire);
        return ep == dp;
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    alignas(64) std::atomic<std::size_t> enqueue_pos_;
    alignas(64) std::atomic<std::size_t> dequeue_pos_;
    Slot slots_[Capacity];
};

} // namespace tinycoro
