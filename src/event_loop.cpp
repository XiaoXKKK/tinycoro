#include "tinycoro/event_loop.h"
#include <cassert>
#include <stdexcept>
#include <cstring>
#include <unistd.h>

#ifdef __linux__
#  include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <sys/event.h>
#  include <sys/time.h>
#else
#  error "Unsupported platform: tinycoro requires Linux (epoll) or macOS/BSD (kqueue)"
#endif

namespace tinycoro {

static constexpr int kMaxEvents = 256;

// ---- Construction / Destruction ----------------------------------------

EventLoop::EventLoop() {
    init_poller();
}

EventLoop::~EventLoop() {
    destroy_poller();
}

// ---- Public API --------------------------------------------------------

void EventLoop::add_channel(Channel* ch) {
    assert(ch && ch->fd >= 0);
    channels_[ch->fd] = ch;
    ctl_add(ch);
}

void EventLoop::update_channel(Channel* ch) {
    assert(ch && ch->fd >= 0);
    channels_[ch->fd] = ch;
    ctl_mod(ch);
}

void EventLoop::remove_channel(Channel* ch) {
    assert(ch && ch->fd >= 0);
    channels_.erase(ch->fd);
    ctl_del(ch->fd);
}

void EventLoop::poll(int timeout_ms) {
    dispatch_events(timeout_ms);
}

void EventLoop::run() {
    running_ = true;
    while (running_) {
        dispatch_events(10); // 10ms tick
    }
}

void EventLoop::stop() {
    running_ = false;
}

// ---- Platform: epoll ---------------------------------------------------
#ifdef __linux__

void EventLoop::init_poller() {
    poller_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (poller_fd_ < 0) {
        throw std::runtime_error("epoll_create1 failed");
    }
}

void EventLoop::destroy_poller() {
    if (poller_fd_ >= 0) {
        close(poller_fd_);
        poller_fd_ = -1;
    }
}

static uint32_t to_epoll_events(Event e) {
    uint32_t ev = EPOLLET; // Edge-triggered
    if (e & Event::READ)  ev |= EPOLLIN;
    if (e & Event::WRITE) ev |= EPOLLOUT;
    return ev;
}

void EventLoop::ctl_add(Channel* ch) {
    struct epoll_event ev{};
    ev.events = to_epoll_events(ch->interest);
    ev.data.fd = ch->fd;
    epoll_ctl(poller_fd_, EPOLL_CTL_ADD, ch->fd, &ev);
}

void EventLoop::ctl_mod(Channel* ch) {
    struct epoll_event ev{};
    ev.events = to_epoll_events(ch->interest);
    ev.data.fd = ch->fd;
    epoll_ctl(poller_fd_, EPOLL_CTL_MOD, ch->fd, &ev);
}

void EventLoop::ctl_del(int fd) {
    epoll_ctl(poller_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

int EventLoop::dispatch_events(int timeout_ms) {
    struct epoll_event events[kMaxEvents];
    int n = epoll_wait(poller_fd_, events, kMaxEvents, timeout_ms);
    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        auto it = channels_.find(fd);
        if (it == channels_.end()) continue;
        Channel* ch = it->second;
        if ((events[i].events & EPOLLIN) && ch->on_read)  ch->on_read();
        if ((events[i].events & EPOLLOUT) && ch->on_write) ch->on_write();
    }
    return n;
}

// ---- Platform: kqueue --------------------------------------------------
#else

void EventLoop::init_poller() {
    poller_fd_ = kqueue();
    if (poller_fd_ < 0) {
        throw std::runtime_error("kqueue failed");
    }
}

void EventLoop::destroy_poller() {
    if (poller_fd_ >= 0) {
        close(poller_fd_);
        poller_fd_ = -1;
    }
}

static void kqueue_ctl(int kqfd, int fd, int16_t filter, uint16_t flags) {
    struct kevent ev{};
    EV_SET(&ev, fd, filter, flags, 0, 0, nullptr);
    kevent(kqfd, &ev, 1, nullptr, 0, nullptr);
}

void EventLoop::ctl_add(Channel* ch) {
    if (ch->interest & Event::READ)
        kqueue_ctl(poller_fd_, ch->fd, EVFILT_READ,  EV_ADD | EV_ENABLE | EV_CLEAR);
    if (ch->interest & Event::WRITE)
        kqueue_ctl(poller_fd_, ch->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR);
}

void EventLoop::ctl_mod(Channel* ch) {
    // For kqueue, re-add with updated flags is idempotent
    ctl_add(ch);
}

void EventLoop::ctl_del(int fd) {
    kqueue_ctl(poller_fd_, fd, EVFILT_READ,  EV_DELETE);
    kqueue_ctl(poller_fd_, fd, EVFILT_WRITE, EV_DELETE);
}

int EventLoop::dispatch_events(int timeout_ms) {
    struct kevent events[kMaxEvents];
    struct timespec ts{};
    struct timespec* pts = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        pts = &ts;
    }
    int n = kevent(poller_fd_, nullptr, 0, events, kMaxEvents, pts);
    for (int i = 0; i < n; ++i) {
        int fd = static_cast<int>(events[i].ident);
        auto it = channels_.find(fd);
        if (it == channels_.end()) continue;
        Channel* ch = it->second;
        if (events[i].filter == EVFILT_READ  && ch->on_read)  ch->on_read();
        if (events[i].filter == EVFILT_WRITE && ch->on_write) ch->on_write();
    }
    return n;
}

#endif // platform

} // namespace tinycoro
