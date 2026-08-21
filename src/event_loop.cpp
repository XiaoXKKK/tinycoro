#include "tinycoro/event_loop.h"
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#ifndef __linux__
#error "Unsupported platform: tinycoro requires Linux (epoll)"
#endif

#include <sys/epoll.h>

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

    const auto [iterator, inserted] = channels_.emplace(channel->fd, channel);
    if (!inserted)
        throw std::logic_error("channel already registered");
    try {
        ctl_add(channel);
    } catch (...) {
        channels_.erase(iterator);
        throw;
    }
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

} // namespace tinycoro
