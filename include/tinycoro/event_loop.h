#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace tinycoro {

// Event types (bitmask)
enum class Event : uint32_t {
    READ  = 0x1,
    WRITE = 0x2,
};

inline Event operator|(Event a, Event b) {
    return static_cast<Event>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(Event a, Event b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// Channel: binds a file descriptor to read/write callbacks.
struct Channel {
    int fd{-1};
    std::function<void()> on_read;
    std::function<void()> on_write;
    Event interest{Event::READ};
};

// EventLoop: wraps epoll (Linux) or kqueue (macOS) with Edge-Triggered mode.
// Not thread-safe — intended to be driven by a single thread.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // Non-copyable
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Add/update/remove fd interest
    void add_channel(Channel* ch);
    void update_channel(Channel* ch);
    void remove_channel(Channel* ch);

    // Wait for events (timeout_ms = -1 → block indefinitely)
    // Dispatches callbacks for all ready events.
    void poll(int timeout_ms = 0);

    // Run the loop forever (until stop() is called)
    void run();

    void stop();

    bool running() const { return running_; }

private:
    int poller_fd_{-1};  // epoll fd or kqueue fd
    bool running_{false};
    std::unordered_map<int, Channel*> channels_;

    // Platform-specific helpers
    void init_poller();
    void destroy_poller();
    void ctl_add(Channel* ch);
    void ctl_mod(Channel* ch);
    void ctl_del(int fd);
    // Dispatch up to max_events ready events; returns number dispatched
    int dispatch_events(int timeout_ms);
};

} // namespace tinycoro
