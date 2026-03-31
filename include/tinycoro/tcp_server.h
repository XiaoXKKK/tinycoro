#pragma once
#include "tinycoro/event_loop.h"
#include "tinycoro/tcp_connection.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace tinycoro {

using ConnectionCallback = std::function<void(TcpConnectionPtr)>;

// -----------------------------------------------------------------------
// TcpServer — listens on a port, accepts connections, and wires each
// connection into the EventLoop with user-provided callbacks.
// -----------------------------------------------------------------------
class TcpServer {
public:
    TcpServer(EventLoop* loop, uint16_t port);
    ~TcpServer();

    void set_connection_callback(ConnectionCallback cb) { conn_cb_ = std::move(cb); }
    void set_message_callback(MessageCallback cb)       { msg_cb_  = std::move(cb); }

    // Start accepting; registers listen socket into the EventLoop
    void start();

private:
    void handle_accept();
    void handle_close(TcpConnectionPtr conn);

    static int create_listen_fd(uint16_t port);
    static void set_nonblocking(int fd);
    static void set_reuse_addr(int fd);

    EventLoop* loop_;
    uint16_t port_;
    int listen_fd_{-1};
    Channel accept_channel_;

    ConnectionCallback conn_cb_;
    MessageCallback    msg_cb_;
    std::unordered_map<int, TcpConnectionPtr> connections_;
};

} // namespace tinycoro
