#include "tinycoro/tcp_connection.h"
#include <cerrno>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace tinycoro {
namespace {
int send_flags() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}
} // namespace

TcpConnection::TcpConnection(int fd, EventLoop* loop) : fd_(fd), loop_(loop) {
    if (fd_ < 0 || !loop_)
        throw std::invalid_argument("invalid TcpConnection");
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    (void)setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    channel_.fd = fd_;
    channel_.interest = Event::READ;
    channel_.on_read = [this] { handle_read(); };
    channel_.on_write = [this] { handle_write(); };
}

TcpConnection::~TcpConnection() {
    if (started_ && !closed_)
        loop_->remove_channel(&channel_);
    if (fd_ >= 0)
        ::close(fd_);
}

void TcpConnection::start() {
    if (started_ || closed_)
        throw std::logic_error("connection cannot be started");
    loop_->add_channel(&channel_);
    started_ = true;
}

void TcpConnection::send(const std::string& data) {
    send(data.data(), data.size());
}

void TcpConnection::send(const char* data, std::size_t length) {
    if (closed_ || length == 0)
        return;

    if (write_buffer_.readable() == 0) {
        ssize_t count = ::send(fd_, data, length, send_flags());
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                close();
                return;
            }
            count = 0;
        }
        data += count;
        length -= static_cast<std::size_t>(count);
    }

    if (length > 0 && !closed_) {
        write_buffer_.append(data, length);
        channel_.interest = Event::READ | Event::WRITE;
        loop_->update_channel(&channel_);
    }
}

void TcpConnection::close() {
    if (closed_)
        return;
    closed_ = true;
    if (started_) {
        loop_->remove_channel(&channel_);
        started_ = false;
    }
    if (close_callback_)
        close_callback_(shared_from_this());
}

void TcpConnection::handle_read() {
    char temporary[4096];
    for (;;) {
        const ssize_t count = ::recv(fd_, temporary, sizeof(temporary), 0);
        if (count > 0) {
            read_buffer_.append(temporary, static_cast<std::size_t>(count));
        } else if (count == 0) {
            close();
            return;
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close();
            return;
        }
    }

    if (read_buffer_.readable() > 0 && message_callback_) {
        message_callback_(shared_from_this(), read_buffer_);
    }
}

void TcpConnection::handle_write() {
    while (write_buffer_.readable() > 0) {
        const ssize_t count =
            ::send(fd_, write_buffer_.read_ptr(), write_buffer_.readable(), send_flags());
        if (count > 0) {
            write_buffer_.consume(static_cast<std::size_t>(count));
        } else if (count < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close();
            return;
        }
    }

    if (!closed_ && write_buffer_.readable() == 0) {
        channel_.interest = Event::READ;
        loop_->update_channel(&channel_);
    }
}

} // namespace tinycoro