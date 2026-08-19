#include "tinycoro/event_loop.h"
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <sys/time.h>
#else
#error "Unsupported platform: tinycoro requires Linux (epoll) or macOS/BSD (kqueue)"
#endif

namespace tinycoro {
namespace {
constexpr int kMaxEvents = 256;
}

EventLoop::EventLoop() {
    init_poller();
}

EventLoop::~EventLoop() {
    destroy_poller();
}

void EventLoop::add_channel(Channel* channel) {
    if (!channel || channel->fd < 0)
        throw std::invalid_argument("invalid channel");
    if (channels_.count(channel->fd) != 0) {
        throw std::logic_error("channel already registered");
    }
    ctl_add(channel);
    channels_.emplace(channel->fd, channel);
}

void EventLoop::update_channel(Channel* channel) {
    if (!channel || channel->fd < 0)
        throw std::invalid_argument("invalid channel");
    auto it = channels_.find(channel->fd);
    if (it == channels_.end() || it->second != channel) {
        throw std::logic_error("channel is not registered");
    }
    ctl_mod(channel);
}

void EventLoop::remove_channel(Channel* channel) {
    if (!channel || channel->fd < 0)
        return;
    auto it = channels_.find(channel->fd);
    if (it == channels_.end() || it->second != channel)
        return;
    channels_.erase(it);
    ctl_del(channel->fd);
}

void EventLoop::poll(int timeout_ms) {
    dispatch_events(timeout_ms);
}

void EventLoop::run() {
    running_ = true;
    while (running_)
        dispatch_events(10);
}

void EventLoop::stop() {
    running_ = false;
}

#ifdef __linux__

void EventLoop::init_poller() {
    poller_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (poller_fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
}

void EventLoop::destroy_poller() {
    if (poller_fd_ >= 0) {
        close(poller_fd_);
        poller_fd_ = -1;
    }
}

namespace {
std::uint32_t to_epoll_events(Event interest) {
    std::uint32_t events = EPOLLET | EPOLLRDHUP;
    if (interest & Event::READ)
        events |= EPOLLIN;
    if (interest & Event::WRITE)
        events |= EPOLLOUT;
    return events;
}
} // namespace

void EventLoop::ctl_add(Channel* channel) {
    epoll_event event{};
    event.events = to_epoll_events(channel->interest);
    event.data.fd = channel->fd;
    if (epoll_ctl(poller_fd_, EPOLL_CTL_ADD, channel->fd, &event) < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_ctl ADD");
    }
}

void EventLoop::ctl_mod(Channel* channel) {
    epoll_event event{};
    event.events = to_epoll_events(channel->interest);
    event.data.fd = channel->fd;
    if (epoll_ctl(poller_fd_, EPOLL_CTL_MOD, channel->fd, &event) < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_ctl MOD");
    }
}

void EventLoop::ctl_del(int fd) noexcept {
    (void)epoll_ctl(poller_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

int EventLoop::dispatch_events(int timeout_ms) {
    epoll_event events[kMaxEvents];
    const int count = epoll_wait(poller_fd_, events, kMaxEvents, timeout_ms);
    if (count < 0) {
        if (errno == EINTR)
            return 0;
        throw std::system_error(errno, std::generic_category(), "epoll_wait");
    }

    for (int i = 0; i < count; ++i) {
        const int fd = events[i].data.fd;
        auto it = channels_.find(fd);
        if (it == channels_.end())
            continue;
        Channel* channel = it->second;
        const bool terminal = (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;

        if (((events[i].events & EPOLLIN) != 0 || terminal) && channel->on_read) {
            auto callback = channel->on_read;
            callback();
        }

        it = channels_.find(fd);
        if (it == channels_.end() || it->second != channel)
            continue;
        if (((events[i].events & EPOLLOUT) != 0 || terminal) && channel->on_write) {
            auto callback = channel->on_write;
            callback();
        }
    }
    return count;
}

#else

void EventLoop::init_poller() {
    poller_fd_ = kqueue();
    if (poller_fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "kqueue");
    }
}

void EventLoop::destroy_poller() {
    if (poller_fd_ >= 0) {
        close(poller_fd_);
        poller_fd_ = -1;
    }
}

namespace {
void kqueue_ctl(int poller_fd, int fd, std::int16_t filter, std::uint16_t flags,
                bool ignore_missing = false) {
    struct kevent event {};
    EV_SET(&event, fd, filter, flags, 0, 0, nullptr);
    if (kevent(poller_fd, &event, 1, nullptr, 0, nullptr) < 0) {
        if (ignore_missing && errno == ENOENT)
            return;
        throw std::system_error(errno, std::generic_category(), "kevent control");
    }
}
} // namespace

void EventLoop::ctl_add(Channel* channel) {
    if (channel->interest & Event::READ) {
        kqueue_ctl(poller_fd_, channel->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR);
    }
    if (channel->interest & Event::WRITE) {
        kqueue_ctl(poller_fd_, channel->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR);
    }
}

void EventLoop::ctl_mod(Channel* channel) {
    if (channel->interest & Event::READ) {
        kqueue_ctl(poller_fd_, channel->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR);
    } else {
        kqueue_ctl(poller_fd_, channel->fd, EVFILT_READ, EV_DELETE, true);
    }
    if (channel->interest & Event::WRITE) {
        kqueue_ctl(poller_fd_, channel->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR);
    } else {
        kqueue_ctl(poller_fd_, channel->fd, EVFILT_WRITE, EV_DELETE, true);
    }
}

void EventLoop::ctl_del(int fd) noexcept {
    struct kevent changes[2]{};
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    (void)kevent(poller_fd_, changes, 2, nullptr, 0, nullptr);
}

int EventLoop::dispatch_events(int timeout_ms) {
    struct kevent events[kMaxEvents];
    timespec timeout{};
    timespec* timeout_ptr = nullptr;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }

    const int count = kevent(poller_fd_, nullptr, 0, events, kMaxEvents, timeout_ptr);
    if (count < 0) {
        if (errno == EINTR)
            return 0;
        throw std::system_error(errno, std::generic_category(), "kevent wait");
    }

    for (int i = 0; i < count; ++i) {
        const int fd = static_cast<int>(events[i].ident);
        auto it = channels_.find(fd);
        if (it == channels_.end())
            continue;
        Channel* channel = it->second;
        const bool terminal = (events[i].flags & (EV_EOF | EV_ERROR)) != 0;

        if ((events[i].filter == EVFILT_READ || terminal) && channel->on_read) {
            auto callback = channel->on_read;
            callback();
        }

        it = channels_.find(fd);
        if (it == channels_.end() || it->second != channel)
            continue;
        if ((events[i].filter == EVFILT_WRITE || terminal) && channel->on_write) {
            auto callback = channel->on_write;
            callback();
        }
    }
    return count;
}

#endif

} // namespace tinycoro