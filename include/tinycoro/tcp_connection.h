#pragma once
#include "tinycoro/buffer.h"
#include "tinycoro/event_loop.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace tinycoro {

class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using MessageCallback = std::function<void(TcpConnectionPtr, Buffer&)>;
using CloseCallback = std::function<void(TcpConnectionPtr)>;

// Callback-oriented connection primitive kept as a Reactor-style comparison
// with TcpStream's coroutine-oriented blocking facade.
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
  public:
    TcpConnection(int fd, EventLoop* loop);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void set_message_callback(MessageCallback callback) { message_callback_ = std::move(callback); }
    void set_close_callback(CloseCallback callback) { close_callback_ = std::move(callback); }

    void start();
    void send(const std::string& data);
    void send(const char* data, std::size_t length);
    void close();

    int fd() const { return fd_; }
    bool closed() const { return closed_; }

  private:
    void handle_read();
    void handle_write();

    int fd_;
    bool started_{false};
    bool closed_{false};
    EventLoop* loop_;
    Channel channel_;
    Buffer read_buffer_;
    Buffer write_buffer_;
    MessageCallback message_callback_;
    CloseCallback close_callback_;
};

} // namespace tinycoro