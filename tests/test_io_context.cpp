#include "tinycoro/buffer.h"
#include "tinycoro/io_context.h"
#include "tinycoro/tcp_stream.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;
using namespace tinycoro;

namespace {

struct SocketPair {
    int fd[2]{-1, -1};

    SocketPair() {
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
            throw std::runtime_error("socketpair failed");
        }
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

} // namespace

TEST(IoContextTest, CooperativeYieldIsFifo) {
    IoContext context;
    std::vector<int> order;

    context.spawn([&] {
        order.push_back(1);
        context.yield();
        order.push_back(3);
    });
    context.spawn([&] {
        order.push_back(2);
        context.yield();
        order.push_back(4);
    });

    context.run();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
}

TEST(IoContextTest, ReadableEventResumesSuspendedCoroutine) {
    SocketPair sockets;
    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    Buffer input;
    IoResult result;

    context.spawn([&] { result = stream->read_some(input, 16, 500ms); });
    context.spawn([&] {
        context.yield();
        ASSERT_EQ(::send(sockets.fd[1], "ready", 5, 0), 5);
    });

    context.run();
    EXPECT_EQ(result.status, IoStatus::Ok);
    EXPECT_EQ(input.retrieve_all_as_string(), "ready");
}

TEST(IoContextTest, ReadDeadlineExpiresWithoutBusyPolling) {
    SocketPair sockets;
    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    Buffer input;
    IoResult result;
    const auto begin = IoContext::Clock::now();

    context.spawn([&] { result = stream->read_some(input, 16, 20ms); });
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

    std::thread reader([&] {
        char buffer[8192];
        while (received.size() < payload.size()) {
            const ssize_t count = ::recv(sockets.fd[1], buffer, sizeof(buffer), 0);
            if (count <= 0)
                break;
            received.append(buffer, static_cast<std::size_t>(count));
        }
    });

    IoResult result;
    context.spawn([&] { result = stream->write_all(payload, 2s); });
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

    context.spawn([&] {
        auto connection = listener.accept(1s);
        ASSERT_NE(connection, nullptr);
        context.spawn([&, connection] {
            Buffer input;
            const auto read = connection->read_some(input, 64, 1s);
            ASSERT_EQ(read.status, IoStatus::Ok);
            const std::string message(input.read_ptr(), read.bytes);
            ASSERT_EQ(connection->write_all(message, 1s).status, IoStatus::Ok);
            context.stop();
        });
    });

    std::thread client([&] {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(listener.port());
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr), 1);
        ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);

        ASSERT_EQ(::send(fd, "loopback", 8, 0), 8);
        char response[8];
        std::size_t received = 0;
        while (received < sizeof(response)) {
            const ssize_t count = ::recv(fd, response + received, sizeof(response) - received, 0);
            ASSERT_GT(count, 0);
            received += static_cast<std::size_t>(count);
        }
        client_response.assign(response, sizeof(response));
        ::close(fd);
    });

    context.run();
    client.join();
    EXPECT_EQ(client_response, "loopback");
}

TEST(IoContextTest, ClosingStreamCancelsSuspendedRead) {
    SocketPair sockets;
    IoContext context;
    auto stream = std::make_shared<TcpStream>(context, sockets.release(0));
    Buffer input;
    IoResult result;

    context.spawn([&] { result = stream->read_some(input, 16, 1s); });
    context.spawn([&] {
        context.yield();
        stream->close();
    });

    context.run();
    EXPECT_EQ(result.status, IoStatus::Closed);
}

TEST(IoContextTest, TaskExceptionsReturnToCaller) {
    IoContext context;
    context.spawn([] { throw std::runtime_error("task failed"); });
    EXPECT_THROW(context.run(), std::runtime_error);
}