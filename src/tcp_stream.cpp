#include "tinycoro/tcp_stream.h"
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>

#include <arpa/inet.h>

namespace tinycoro {
namespace {

class Deadline {
  public:
    explicit Deadline(IoContext::Duration timeout)
        : enabled_(timeout >= IoContext::Duration::zero()),
          deadline_(enabled_ ? IoContext::Clock::now() + timeout : IoContext::Clock::time_point{}) {
    }

    IoContext::Duration remaining() const {
        if (!enabled_)
            return IoContext::kNoTimeout;
        const auto now = IoContext::Clock::now();
        if (now >= deadline_)
            return IoContext::Duration::zero();
        const auto remaining = deadline_ - now;
        auto milliseconds = std::chrono::duration_cast<IoContext::Duration>(remaining);
        if (milliseconds < remaining)
            milliseconds += IoContext::Duration{1};
        return milliseconds;
    }

  private:
    bool enabled_;
    IoContext::Clock::time_point deadline_;
};

void set_nonblocking_and_cloexec(int fd) {
    const int status_flags = fcntl(fd, F_GETFL, 0);
    if (status_flags < 0 || fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) < 0)
        throw std::system_error(errno, std::generic_category(), "fcntl O_NONBLOCK");

    const int descriptor_flags = fcntl(fd, F_GETFD, 0);
    if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0)
        throw std::system_error(errno, std::generic_category(), "fcntl FD_CLOEXEC");
}

} // namespace

class TcpStream::OwnedFd {
  public:
    explicit OwnedFd(int fd) noexcept : fd_(fd) {}

    ~OwnedFd() {
        if (fd_ >= 0)
            ::close(fd_);
    }

    OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    OwnedFd& operator=(OwnedFd&&) = delete;

    int release() noexcept { return std::exchange(fd_, -1); }

  private:
    int fd_;
};

TcpStream::TcpStream(IoContext& context, OwnedFd fd) noexcept
    : context_(&context), fd_(fd.release()) {
}

std::shared_ptr<TcpStream> TcpStream::adopt(IoContext& context, int fd) {
    OwnedFd owned(fd);
    // Construct the RAII guard before allocating. If either allocation for the
    // object or the shared_ptr control block fails, the accepted fd is closed.
    return std::shared_ptr<TcpStream>(new TcpStream(context, std::move(owned)));
}

TcpStream::TcpStream(IoContext& context, int fd) : context_(&context), fd_(fd) {
    if (fd_ < 0)
        throw std::invalid_argument("TcpStream requires a valid fd");
    try {
        set_nonblocking_and_cloexec(fd_);
    } catch (...) {
        ::close(fd_);
        fd_ = -1;
        throw;
    }
}

TcpStream::~TcpStream() {
    close();
}

Task<IoResult> TcpStream::read_some(Buffer& output, std::size_t max_bytes,
                                    IoContext::Duration timeout) {
    if (fd_ < 0)
        co_return IoResult{IoStatus::Closed, 0, EBADF};
    if (max_bytes == 0)
        co_return IoResult{IoStatus::Ok, 0, 0};

    Deadline deadline(timeout);
    output.ensure_writable(max_bytes);
    for (;;) {
        const ssize_t count = ::recv(fd_, output.write_ptr(), max_bytes, 0);
        if (count > 0) {
            output.has_written(static_cast<std::size_t>(count));
            co_return IoResult{IoStatus::Ok, static_cast<std::size_t>(count), 0};
        }
        if (count == 0)
            co_return IoResult{IoStatus::Eof, 0, 0};
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return IoResult{IoStatus::Error, 0, errno};

        if (!(co_await context_->wait_readable(fd_, deadline.remaining()))) {
            co_return fd_ < 0 ? IoResult{IoStatus::Closed, 0, EBADF}
                              : IoResult{IoStatus::Timeout, 0, 0};
        }
        if (fd_ < 0)
            co_return IoResult{IoStatus::Closed, 0, EBADF};
    }
}

Task<IoResult> TcpStream::write_all(std::string_view data, IoContext::Duration timeout) {
    if (fd_ < 0)
        co_return IoResult{IoStatus::Closed, 0, EBADF};

    Deadline deadline(timeout);
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t count =
            ::send(fd_, data.data() + written, data.size() - written, MSG_NOSIGNAL);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            co_return IoResult{IoStatus::Error, written, errno};

        if (!(co_await context_->wait_writable(fd_, deadline.remaining()))) {
            co_return fd_ < 0 ? IoResult{IoStatus::Closed, written, EBADF}
                              : IoResult{IoStatus::Timeout, written, 0};
        }
        if (fd_ < 0)
            co_return IoResult{IoStatus::Closed, written, EBADF};
    }
    co_return IoResult{IoStatus::Ok, written, 0};
}

void TcpStream::set_nodelay(bool enabled) {
    if (fd_ < 0)
        throw std::logic_error("TcpStream is closed");
    const int value = enabled ? 1 : 0;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) < 0)
        throw std::system_error(errno, std::generic_category(), "setsockopt TCP_NODELAY");
}

void TcpStream::close() {
    if (fd_ < 0)
        return;
    context_->cancel(fd_);
    (void)::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
}

TcpListener::TcpListener(IoContext& context, std::uint16_t port, std::string bind_address,
                         int backlog)
    : context_(&context) {
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0)
        throw std::system_error(errno, std::generic_category(), "socket");

    try {
        const int reuse = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
            throw std::system_error(errno, std::generic_category(), "setsockopt SO_REUSEADDR");

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1)
            throw std::invalid_argument("invalid IPv4 bind address");
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::system_error(errno, std::generic_category(), "bind");
        if (::listen(fd_, backlog) < 0)
            throw std::system_error(errno, std::generic_category(), "listen");

        socklen_t address_size = sizeof(address);
        if (getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &address_size) < 0)
            throw std::system_error(errno, std::generic_category(), "getsockname");
        port_ = ntohs(address.sin_port);
    } catch (...) {
        ::close(fd_);
        fd_ = -1;
        throw;
    }
}

TcpListener::~TcpListener() {
    close();
}

Task<std::shared_ptr<TcpStream>> TcpListener::accept(IoContext::Duration timeout) {
    if (fd_ < 0)
        throw std::logic_error("TcpListener is closed");
    Deadline deadline(timeout);

    for (;;) {
        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        const int connection = ::accept4(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_size,
                                         SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connection >= 0)
            co_return TcpStream::adopt(*context_, connection);
        if (errno == EINTR || errno == ECONNABORTED)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            throw std::system_error(errno, std::generic_category(), "accept4");

        const bool ready = co_await context_->wait_readable(fd_, deadline.remaining());
        if (!ready) {
            if (fd_ < 0)
                throw std::logic_error("TcpListener was closed while awaiting accept");
            co_return nullptr;
        }
        if (fd_ < 0)
            throw std::logic_error("TcpListener was closed while awaiting accept");
    }
}

void TcpListener::close() {
    if (fd_ < 0)
        return;
    context_->cancel(fd_);
    ::close(fd_);
    fd_ = -1;
}

} // namespace tinycoro
