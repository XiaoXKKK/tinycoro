#include "tinycoro/tcp_stream.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

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
        auto millis = std::chrono::duration_cast<IoContext::Duration>(remaining);
        if (millis < remaining)
            millis += IoContext::Duration{1};
        return millis;
    }

  private:
    bool enabled_;
    IoContext::Clock::time_point deadline_;
};

void set_nonblocking_and_cloexec(int fd) {
    const int status_flags = fcntl(fd, F_GETFL, 0);
    if (status_flags < 0 || fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl O_NONBLOCK");
    }
    const int descriptor_flags = fcntl(fd, F_GETFD, 0);
    if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl FD_CLOEXEC");
    }
}

int send_flags() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

} // namespace

TcpStream::TcpStream(IoContext& context, int fd) : context_(&context), fd_(fd) {
    if (fd_ < 0)
        throw std::invalid_argument("TcpStream requires a valid fd");
    try {
        set_nonblocking_and_cloexec(fd_);
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        (void)setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    } catch (...) {
        ::close(fd_);
        fd_ = -1;
        throw;
    }
}

TcpStream::~TcpStream() {
    close();
}

IoResult TcpStream::read_some(Buffer& output, std::size_t max_bytes, IoContext::Duration timeout) {
    if (fd_ < 0)
        return {IoStatus::Closed, 0, EBADF};
    if (max_bytes == 0)
        return {IoStatus::Ok, 0, 0};

    Deadline deadline(timeout);
    output.ensure_writable(max_bytes);
    for (;;) {
        const ssize_t count = ::recv(fd_, output.write_ptr(), max_bytes, 0);
        if (count > 0) {
            output.has_written(static_cast<std::size_t>(count));
            return {IoStatus::Ok, static_cast<std::size_t>(count), 0};
        }
        if (count == 0)
            return {IoStatus::Eof, 0, 0};
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return {IoStatus::Error, 0, errno};
        }
        if (!context_->wait_readable(fd_, deadline.remaining())) {
            return fd_ < 0 ? IoResult{IoStatus::Closed, 0, EBADF}
                           : IoResult{IoStatus::Timeout, 0, 0};
        }
        if (fd_ < 0)
            return {IoStatus::Closed, 0, EBADF};
    }
}

IoResult TcpStream::write_all(std::string_view data, IoContext::Duration timeout) {
    if (fd_ < 0)
        return {IoStatus::Closed, 0, EBADF};

    Deadline deadline(timeout);
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t count =
            ::send(fd_, data.data() + written, data.size() - written, send_flags());
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            return {IoStatus::Error, written, errno};
        }
        if (!context_->wait_writable(fd_, deadline.remaining())) {
            return fd_ < 0 ? IoResult{IoStatus::Closed, written, EBADF}
                           : IoResult{IoStatus::Timeout, written, 0};
        }
        if (fd_ < 0)
            return {IoStatus::Closed, written, EBADF};
    }
    return {IoStatus::Ok, written, 0};
}

void TcpStream::set_nodelay(bool enabled) {
    if (fd_ < 0)
        throw std::logic_error("TcpStream is closed");
    const int value = enabled ? 1 : 0;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt TCP_NODELAY");
    }
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
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    try {
        set_nonblocking_and_cloexec(fd_);
        const int reuse = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            throw std::system_error(errno, std::generic_category(), "setsockopt SO_REUSEADDR");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1) {
            throw std::invalid_argument("invalid IPv4 bind address");
        }
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }
        if (::listen(fd_, backlog) < 0) {
            throw std::system_error(errno, std::generic_category(), "listen");
        }

        socklen_t address_size = sizeof(address);
        if (getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &address_size) < 0) {
            throw std::system_error(errno, std::generic_category(), "getsockname");
        }
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

std::shared_ptr<TcpStream> TcpListener::accept(IoContext::Duration timeout) {
    if (fd_ < 0)
        throw std::logic_error("TcpListener is closed");
    Deadline deadline(timeout);

    for (;;) {
        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        const int connection = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (connection >= 0) {
            return std::make_shared<TcpStream>(*context_, connection);
        }
        if (errno == EINTR || errno == ECONNABORTED)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            throw std::system_error(errno, std::generic_category(), "accept");
        }
        if (!context_->wait_readable(fd_, deadline.remaining()))
            return nullptr;
        if (fd_ < 0)
            throw std::logic_error("TcpListener was closed while waiting");
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