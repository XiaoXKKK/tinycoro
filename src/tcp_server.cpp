#include "tinycoro/tcp_server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace tinycoro {

TcpServer::TcpServer(EventLoop* loop, uint16_t port)
    : loop_(loop), port_(port) {
    listen_fd_ = create_listen_fd(port_);
    accept_channel_.fd = listen_fd_;
    accept_channel_.interest = Event::READ;
    accept_channel_.on_read = [this] { handle_accept(); };
}

TcpServer::~TcpServer() {
    if (listen_fd_ >= 0) {
        loop_->remove_channel(&accept_channel_);
        ::close(listen_fd_);
    }
}

void TcpServer::start() {
    loop_->add_channel(&accept_channel_);
}

void TcpServer::handle_accept() {
    // ET mode: accept in a loop until EAGAIN
    for (;;) {
        struct sockaddr_in addr{};
        socklen_t addrlen = sizeof(addr);
        int conn_fd = ::accept(listen_fd_,
                               reinterpret_cast<sockaddr*>(&addr), &addrlen);
        if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break; // unexpected error; just stop accepting this round
        }

        set_nonblocking(conn_fd);

        auto conn = std::make_shared<TcpConnection>(conn_fd, loop_);
        connections_[conn_fd] = conn;

        if (msg_cb_)  conn->set_message_callback(msg_cb_);
        conn->set_close_callback([this](TcpConnectionPtr c) {
            handle_close(c);
        });

        conn->start();

        if (conn_cb_) conn_cb_(conn);
    }
}

void TcpServer::handle_close(TcpConnectionPtr conn) {
    connections_.erase(conn->fd());
}

// ---- helpers -----------------------------------------------------------

int TcpServer::create_listen_fd(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");

    set_reuse_addr(fd);
    set_nonblocking(fd);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind() failed");
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        ::close(fd);
        throw std::runtime_error("listen() failed");
    }
    return fd;
}

void TcpServer::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void TcpServer::set_reuse_addr(int fd) {
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

} // namespace tinycoro
