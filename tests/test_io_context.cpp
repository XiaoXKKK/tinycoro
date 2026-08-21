#include "tinycoro/buffer.h"
#include "tinycoro/io_context.h"
#include "tinycoro/task.h"
#include "tinycoro/tcp_stream.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std::chrono_literals;
using namespace tinycoro;

namespace {

struct SocketPair {
    int fd[2]{-1, -1};

    SocketPair() {
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0)
            throw std::system_error(errno, std::generic_category(), "socketpair");
    }

    ~SocketPair() {
        for (int& value : fd) {
            if (value >= 0)
                ::close(value);
            value = -1;
        }
    }

    int release(int index) {
        const int result = fd[index];
        fd[index] = -1;
        return result;
    }
};

void require(bool condition, const char* operation) {
    if (!condition)
        throw std::system_error(errno, std::generic_category(), operation);
}

void fill_send_buffer(int fd) {
    const char data[4096]{};
    for (;;) {
        const ssize_t count = ::send(fd, data, sizeof(data), MSG_NOSIGNAL);
        if (count > 0)
            continue;
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        throw std::system_error(errno, std::generic_category(), "fill send buffer");
    }
}

Task<void> read_once(std::shared_ptr<TcpStream> stream, Buffer& input, IoResult& result,
                     IoContext::Duration timeout) {
    result = co_await stream->read_some(input, 16, timeout);
}

Task<void> write_once(std::shared_ptr<TcpStream> stream, std::string_view data, IoResult& result,
                      IoContext::Duration timeout) {
    result = co_await stream->write_all(data, timeout);
}

Task<void> send_after_yield(IoContext& context, int fd, std::string_view data) {
    co_await context.yield();
    require(::send(fd, data.data(), data.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(data.size()),
            "send");
}

Task<void> drive_read_and_write(int peer_fd) {
    require(::send(peer_fd, "r", 1, MSG_NOSIGNAL) == 1, "send");
    char drained[64 * 1024];
    require(::recv(peer_fd, drained, sizeof(drained), 0) > 0, "recv");
    co_return;
}

Task<void> close_stream(std::shared_ptr<TcpStream> stream, IoContext* context = nullptr) {
    if (context)
        co_await context->yield();
    stream->close();
}

Task<void> echo_connection(IoContext& context, std::shared_ptr<TcpStream> connection) {
    Buffer input;
    const auto read = co_await connection->read_some(input, 64, 1s);
    if (!read)
        throw std::runtime_error("loopback read failed");
    const std::string message(input.read_ptr(), read.bytes);
    if (!(co_await connection->write_all(message, 1s)))
        throw std::runtime_error("loopback write failed");
    context.stop();
}

Task<void> accept_one(IoContext& context, TcpListener& listener) {
    auto connection = co_await listener.accept(1s);
    if (!connection)
        throw std::runtime_error("loopback accept timed out");
    context.spawn(echo_connection(context, std::move(connection)));
}

Task<void> accept_until_closed(TcpListener& listener) {
    (void)co_await listener.accept(1s);
}

Task<void> close_listener_after_yield(IoContext& context, TcpListener& listener) {
    co_await context.yield();
    listener.close();
}

} // namespace

TEST(IoContextTest, ReadableEventResumesSuspendedTask) {
    SocketPair sockets;
    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    Buffer input;
    IoResult result;

    context.spawn(read_once(stream, input, result, 500ms));
    context.spawn(send_after_yield(context, sockets.fd[1], "ready"));
    context.run();

    EXPECT_EQ(result.status, IoStatus::Ok);
    EXPECT_EQ(input.retrieve_all_as_string(), "ready");
}

TEST(IoContextTest, ReadAndWriteWaitersCanShareOneFd) {
    SocketPair sockets;
    int send_buffer = 4096;
    ASSERT_EQ(setsockopt(sockets.fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)),
              0);

    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    fill_send_buffer(stream->native_handle());

    Buffer input;
    IoResult read_result;
    IoResult write_result;
    context.spawn(read_once(stream, input, read_result, 500ms));
    context.spawn(write_once(stream, "w", write_result, 500ms));
    context.spawn(drive_read_and_write(sockets.fd[1]));
    context.run();

    EXPECT_EQ(read_result.status, IoStatus::Ok);
    EXPECT_EQ(write_result.status, IoStatus::Ok);
    EXPECT_EQ(write_result.bytes, 1u);
    EXPECT_EQ(input.retrieve_all_as_string(), "r");
}

TEST(IoContextTest, ReadDeadlineExpiresWithoutBusyPolling) {
    SocketPair sockets;
    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    Buffer input;
    IoResult result;
    const auto begin = IoContext::Clock::now();

    context.spawn(read_once(stream, input, result, 20ms));
    context.run();

    const auto elapsed = IoContext::Clock::now() - begin;
    EXPECT_EQ(result.status, IoStatus::Timeout);
    EXPECT_GE(elapsed, 15ms);
    EXPECT_LT(elapsed, 500ms);
}

TEST(IoContextTest, PartialWritesApplyBackpressureAndResume) {
    SocketPair sockets;
    int send_buffer = 4096;
    ASSERT_EQ(setsockopt(sockets.fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)),
              0);

    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    const std::string payload(1024 * 1024, 'x');
    std::string received;
    received.reserve(payload.size());

    std::jthread reader([&] {
        char buffer[8192];
        while (received.size() < payload.size()) {
            const ssize_t count = ::recv(sockets.fd[1], buffer, sizeof(buffer), 0);
            if (count <= 0)
                break;
            received.append(buffer, static_cast<std::size_t>(count));
        }
    });

    IoResult result;
    context.spawn(write_once(stream, payload, result, 2s));
    context.run();
    stream->close();
    reader.join();

    EXPECT_EQ(result.status, IoStatus::Ok);
    EXPECT_EQ(result.bytes, payload.size());
    EXPECT_EQ(received, payload);
}

TEST(IoContextTest, TcpListenerAndStreamCompleteLoopbackEcho) {
    IoContext context;
    TcpListener listener(context, 0, "127.0.0.1");
    std::string client_response;
    std::exception_ptr client_error;

    context.spawn(accept_one(context, listener));
    std::jthread client([&] {
        int fd = -1;
        try {
            fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            require(fd >= 0, "socket");
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(listener.port());
            require(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1, "inet_pton");
            require(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
                    "connect");
            require(::send(fd, "loopback", 8, MSG_NOSIGNAL) == 8, "send");

            char response[8];
            std::size_t received = 0;
            while (received < sizeof(response)) {
                const ssize_t count =
                    ::recv(fd, response + received, sizeof(response) - received, 0);
                require(count > 0, "recv");
                received += static_cast<std::size_t>(count);
            }
            client_response.assign(response, sizeof(response));
            ::close(fd);
        } catch (...) {
            if (fd >= 0)
                ::close(fd);
            client_error = std::current_exception();
        }
    });

    context.run();
    client.join();
    if (client_error)
        std::rethrow_exception(client_error);
    EXPECT_EQ(client_response, "loopback");
}

TEST(IoContextTest, ClosingListenerCancelsSuspendedAccept) {
    IoContext context;
    TcpListener listener(context, 0, "127.0.0.1");
    context.spawn(accept_until_closed(listener));
    context.spawn(close_listener_after_yield(context, listener));

    EXPECT_THROW(context.run(), std::logic_error);
    EXPECT_EQ(context.task_count(), 0u);
}

TEST(IoContextTest, ClosingStreamCancelsSuspendedRead) {
    SocketPair sockets;
    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    Buffer input;
    IoResult result;

    context.spawn(read_once(stream, input, result, 1s));
    context.spawn(close_stream(stream, &context));
    context.run();
    EXPECT_EQ(result.status, IoStatus::Closed);
}

TEST(IoContextTest, ClosingStreamCancelsReadAndWriteWaiters) {
    SocketPair sockets;
    int send_buffer = 4096;
    ASSERT_EQ(setsockopt(sockets.fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)),
              0);

    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    fill_send_buffer(stream->native_handle());

    Buffer input;
    IoResult read_result;
    IoResult write_result;
    context.spawn(read_once(stream, input, read_result, 1s));
    context.spawn(write_once(stream, "w", write_result, 1s));
    context.spawn(close_stream(stream));
    context.run();

    EXPECT_EQ(read_result.status, IoStatus::Closed);
    EXPECT_EQ(write_result.status, IoStatus::Closed);
    EXPECT_FALSE(stream->is_open());
}
