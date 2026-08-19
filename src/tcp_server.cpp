#include "tinycoro/tcp_server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

namespace tinycoro {

TcpServer::TcpServer(EventLoop* loop, std::uint16_t port) : loop_(loop), port_(port) {
    if (!loop_)
        throw std::invalid_argument("TcpServer requires an EventLoop");
    listen_fd_ = create_listen_fd(port_);
    accept_channel_.fd = listen_fd_;
    accept_channel_.interest = Event::READ;
    accept_channel_.on_read = [this] { handle_accept(); };
}

TcpServer::~TcpServer() {
    // Destruction is not a user-observable close event and must not call code
    // that can throw while unwinding the server.
    close_callback_ = {};
    while (!connections_.empty())
        connections_.begin()->second->close();
    if (started_)
        loop_->remove_channel(&accept_channel_);
    if (listen_fd_ >= 0)
        ::close(listen_fd_);
}

void TcpServer::start() {
    if (started_)
        throw std::logic_error("TcpServer already started");
    loop_->add_channel(&accept_channel_);
    started_ = true;
}

void TcpServer::handle_accept() {
    for (;;) {
        sockaddr_in address{};
        socklen_t address_size = sizeof(address);
        const int connection_fd =
            ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_size);
        if (connection_fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            break;
        }

        try {
            set_nonblocking(connection_fd);
        } catch (...) {
            ::close(connection_fd);
            throw;
        }

        std::shared_ptr<TcpConnection> connection;
        try {
            connection = std::make_shared<TcpConnection>(connection_fd, loop_);
            connection->set_close_callback(
                [this](TcpConnectionPtr closed) { handle_close(std::move(closed)); });
            if (message_callback_) {
                connection->set_message_callback(message_callback_);
            }
            connections_.emplace(connection_fd, connection);
            connection->start();
            if (connection_callback_)
                connection_callback_(connection);
        } catch (...) {
            if (connection && !connection->closed())
                connection->close();
            throw;
        }
    }
}

void TcpServer::handle_close(TcpConnectionPtr connection) {
    auto callback = close_callback_;
    connections_.erase(connection->fd());
    if (callback)
        callback(std::move(connection));
}

int TcpServer::create_listen_fd(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), "socket");

    try {
        set_reuse_addr(fd);
        set_nonblocking(fd);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }
        if (::listen(fd, SOMAXCONN) < 0) {
            throw std::system_error(errno, std::generic_category(), "listen");
        }
    } catch (...) {
        ::close(fd);
        throw;
    }
    return fd;
}

void TcpServer::set_nonblocking(int fd) {
    const int status_flags = fcntl(fd, F_GETFL, 0);
    if (status_flags < 0 || fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl O_NONBLOCK");
    }
    const int descriptor_flags = fcntl(fd, F_GETFD, 0);
    if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl FD_CLOEXEC");
    }
}

void TcpServer::set_reuse_addr(int fd) {
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt SO_REUSEADDR");
    }
}

} // namespace tinycoro