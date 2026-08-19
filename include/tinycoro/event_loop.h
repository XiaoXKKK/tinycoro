#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace tinycoro {

enum class Event : std::uint32_t {
    READ = 0x1,
    WRITE = 0x2,
};

inline Event operator|(Event lhs, Event rhs) {
    return static_cast<Event>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline bool operator&(Event lhs, Event rhs) {
    return (static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs)) != 0;
}

struct Channel {
    int fd{-1};
    std::function<void()> on_read;
    std::function<void()> on_write;
    Event interest{Event::READ};
};

// Single-threaded edge-triggered readiness loop.
class EventLoop {
  public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void add_channel(Channel* channel);
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);

    void poll(int timeout_ms = 0);
    void run();
    void stop();

    bool running() const { return running_; }

  private:
    int poller_fd_{-1};
    bool running_{false};
    std::unordered_map<int, Channel*> channels_;

    void init_poller();
    void destroy_poller();
    void ctl_add(Channel* channel);
    void ctl_mod(Channel* channel);
    void ctl_del(int fd) noexcept;
    int dispatch_events(int timeout_ms);
};

} // namespace tinycoro