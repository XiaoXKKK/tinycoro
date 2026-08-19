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

    explicit operator bool() const { return status == IoStatus::Ok; }
};

// Move-free shared ownership is intentional: connection tasks are stored in
// std::function and the fd must remain alive across coroutine suspension.
class TcpStream {
  public:
    TcpStream(IoContext& context, int fd);
    ~TcpStream();

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    IoResult read_some(Buffer& output, std::size_t max_bytes = 16 * 1024,
                       IoContext::Duration timeout = IoContext::kNoTimeout);
    IoResult write_all(std::string_view data, IoContext::Duration timeout = IoContext::kNoTimeout);

    void set_nodelay(bool enabled);
    void close();

    int native_handle() const { return fd_; }
    bool is_open() const { return fd_ >= 0; }

  private:
    IoContext* context_;
    int fd_{-1};
};

class TcpListener {
  public:
    TcpListener(IoContext& context, std::uint16_t port, std::string bind_address = "0.0.0.0",
                int backlog = 128);
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // A null result means the accept deadline expired.
    std::shared_ptr<TcpStream> accept(IoContext::Duration timeout = IoContext::kNoTimeout);

    void close();
    std::uint16_t port() const { return port_; }
    int native_handle() const { return fd_; }

  private:
    IoContext* context_;
    int fd_{-1};
    std::uint16_t port_{0};
};

} // namespace tinycoro