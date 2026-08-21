#pragma once

#include "tinycoro/buffer.h"
#include "tinycoro/io_context.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace tinycoro {

enum class IoStatus {
    Ok,
    Eof,
    Timeout,
    Closed,
    Error,
};

struct IoResult {
    IoStatus status{IoStatus::Ok};
    std::size_t bytes{0};
    int error_code{0};

    explicit operator bool() const noexcept { return status == IoStatus::Ok; }
};

// A shared TcpStream keeps the descriptor alive while a connection Task is
// suspended. write_all borrows its string_view until the returned Task finishes.
class TcpStream {
  public:
    TcpStream(IoContext& context, int fd);
    ~TcpStream();

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    Task<IoResult> read_some(Buffer& output, std::size_t max_bytes = 16 * 1024,
                             IoContext::Duration timeout = IoContext::kNoTimeout);
    Task<IoResult> write_all(std::string_view data,
                             IoContext::Duration timeout = IoContext::kNoTimeout);

    void set_nodelay(bool enabled);
    void close();

    int native_handle() const noexcept { return fd_; }
    bool is_open() const noexcept { return fd_ >= 0; }

  private:
    class OwnedFd;

    TcpStream(IoContext& context, OwnedFd fd) noexcept;
    static std::shared_ptr<TcpStream> adopt(IoContext& context, int fd);

    IoContext* context_;
    int fd_{-1};

    friend class TcpListener;
};

class TcpListener {
  public:
    TcpListener(IoContext& context, std::uint16_t port, std::string bind_address = "0.0.0.0",
                int backlog = 128);
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // A null result means the accept deadline expired. Closing the listener
    // while suspended raises a logic_error at the awaiting operation boundary.
    Task<std::shared_ptr<TcpStream>> accept(IoContext::Duration timeout = IoContext::kNoTimeout);

    void close();
    std::uint16_t port() const noexcept { return port_; }
    int native_handle() const noexcept { return fd_; }

  private:
    IoContext* context_;
    int fd_{-1};
    std::uint16_t port_{0};
};

} // namespace tinycoro
