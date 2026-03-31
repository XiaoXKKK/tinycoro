#include "tinycoro/tcp_connection.h"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace tinycoro {

TcpConnection::TcpConnection(int fd, EventLoop* loop)
    : fd_(fd), loop_(loop) {
    channel_.fd = fd_;
    channel_.interest = Event::READ;
    channel_.on_read  = [this] { handle_read(); };
    channel_.on_write = [this] { handle_write(); };
}

TcpConnection::~TcpConnection() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void TcpConnection::start() {
    loop_->add_channel(&channel_);
}

void TcpConnection::send(const std::string& data) {
    send(data.data(), data.size());
}

void TcpConnection::send(const char* data, std::size_t len) {
    if (closed_) return;

    // Try writing directly first (avoids buffering when write buffer is empty)
    if (write_buf_.readable() == 0) {
        ssize_t n = ::write(fd_, data, len);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close();
                return;
            }
            n = 0;
        }
        data += n;
        len  -= static_cast<std::size_t>(n);
    }

    if (len > 0) {
        write_buf_.append(data, len);
        // Enable WRITE event so the loop will drain the buffer
        channel_.interest = Event::READ | Event::WRITE;
        loop_->update_channel(&channel_);
    }
}

void TcpConnection::close() {
    if (closed_) return;
    closed_ = true;
    loop_->remove_channel(&channel_);
    if (close_cb_) close_cb_(shared_from_this());
}

void TcpConnection::handle_read() {
    // ET mode: must drain until EAGAIN
    char tmp[4096];
    for (;;) {
        ssize_t n = ::read(fd_, tmp, sizeof(tmp));
        if (n > 0) {
            read_buf_.append(tmp, static_cast<std::size_t>(n));
        } else if (n == 0) {
            // Peer closed connection
            close();
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // no more data for now
            }
            close();
            return;
        }
    }

    if (read_buf_.readable() > 0 && message_cb_) {
        message_cb_(shared_from_this(), read_buf_);
    }
}

void TcpConnection::handle_write() {
    // Drain write buffer
    while (write_buf_.readable() > 0) {
        ssize_t n = ::write(fd_, write_buf_.read_ptr(), write_buf_.readable());
        if (n > 0) {
            write_buf_.consume(static_cast<std::size_t>(n));
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close();
            return;
        }
    }

    if (write_buf_.readable() == 0) {
        // Nothing left to write; stop monitoring WRITE events
        channel_.interest = Event::READ;
        loop_->update_channel(&channel_);
    }
}

} // namespace tinycoro
