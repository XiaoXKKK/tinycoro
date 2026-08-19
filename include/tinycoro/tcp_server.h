#pragma once
#include "tinycoro/event_loop.h"
#include "tinycoro/tcp_connection.h"
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace tinycoro {

using ConnectionCallback = std::function<void(TcpConnectionPtr)>;

// Callback-based TCP server used by the Reactor examples.
class TcpServer {
  public:
    TcpServer(EventLoop* loop, std::uint16_t port);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void set_connection_callback(ConnectionCallback callback) {
        connection_callback_ = std::move(callback);
    }
    void set_message_callback(MessageCallback callback) { message_callback_ = std::move(callback); }
    void set_close_callback(ConnectionCallback callback) { close_callback_ = std::move(callback); }

    void start();

  private:
    void handle_accept();
    void handle_close(TcpConnectionPtr connection);

    static int create_listen_fd(std::uint16_t port);
    static void set_nonblocking(int fd);
    static void set_reuse_addr(int fd);

    EventLoop* loop_;
    std::uint16_t port_;
    int listen_fd_{-1};
    bool started_{false};
    Channel accept_channel_;
    ConnectionCallback connection_callback_;
    ConnectionCallback close_callback_;
    MessageCallback message_callback_;
    std::unordered_map<int, TcpConnectionPtr> connections_;
};

} // namespace tinycoro