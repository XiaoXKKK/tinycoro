#pragma once
#include "tinycoro/buffer.h"
#include "tinycoro/event_loop.h"
#include <functional>
#include <memory>
#include <string>
#include <unistd.h>

namespace tinycoro {

class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using MessageCallback  = std::function<void(TcpConnectionPtr, Buffer&)>;
using CloseCallback    = std::function<void(TcpConnectionPtr)>;

// -----------------------------------------------------------------------
// TcpConnection — represents one accepted TCP connection.
// Manages non-blocking read/write with Buffer and integrates with EventLoop.
// -----------------------------------------------------------------------
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(int fd, EventLoop* loop);
    ~TcpConnection();

    // Non-copyable
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    void set_close_callback(CloseCallback cb)      { close_cb_ = std::move(cb); }

    // Start monitoring read events
    void start();

    // Send data (enqueues to write buffer, enables WRITE event if needed)
    void send(const std::string& data);
    void send(const char* data, std::size_t len);

    void close();

    int fd() const { return fd_; }
    bool closed() const { return closed_; }

private:
    void handle_read();
    void handle_write();

    int fd_;
    bool closed_{false};
    EventLoop* loop_;
    Channel channel_;
    Buffer read_buf_;
    Buffer write_buf_;
    MessageCallback message_cb_;
    CloseCallback   close_cb_;
};

} // namespace tinycoro
