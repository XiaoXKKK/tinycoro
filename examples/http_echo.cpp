#include "tinycoro/buffer.h"
#include "tinycoro/http_parser.h"
#include "tinycoro/io_context.h"
#include "tinycoro/task.h"
#include "tinycoro/tcp_stream.h"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {
volatile std::sig_atomic_t stop_requested = 0;

void on_signal(int) {
    stop_requested = 1;
}

tinycoro::Task<bool> send_bad_request(const std::shared_ptr<tinycoro::TcpStream>& stream) {
    constexpr std::string_view response = "HTTP/1.1 400 Bad Request\r\n"
                                          "Content-Length: 0\r\n"
                                          "Connection: close\r\n\r\n";
    co_return static_cast<bool>(co_await stream->write_all(response));
}

tinycoro::Task<void> serve_http(std::shared_ptr<tinycoro::TcpStream> stream) {
    using namespace std::chrono_literals;
    tinycoro::Buffer input;
    tinycoro::HttpParser parser;

    for (;;) {
        const auto parsed = parser.parse(input);
        if (parsed == tinycoro::HttpParser::COMPLETE) {
            const auto& request = parser.request();
            const std::string body =
                "method=" + request.method + " path=" + request.path + "\n" + request.body;
            const std::string response = "HTTP/1.1 200 OK\r\n"
                                         "Content-Type: text/plain\r\n"
                                         "Content-Length: " +
                                         std::to_string(body.size()) +
                                         "\r\n"
                                         "Connection: keep-alive\r\n\r\n" +
                                         body;
            if (!(co_await stream->write_all(response)))
                co_return;
            parser.reset();
            continue;
        }
        if (parsed == tinycoro::HttpParser::ERROR) {
            (void)co_await send_bad_request(stream);
            co_return;
        }

        if (!(co_await stream->read_some(input, 16 * 1024, 30s)))
            co_return;
    }
}

tinycoro::Task<void> accept_connections(tinycoro::IoContext& context,
                                        tinycoro::TcpListener& listener) {
    using namespace std::chrono_literals;
    while (!stop_requested) {
        auto connection = co_await listener.accept(250ms);
        if (!connection)
            continue;
        connection->set_nodelay(true);
        context.spawn(serve_http(std::move(connection)));
    }
    context.stop();
}
} // namespace

int main(int argc, char* argv[]) {
    const auto port =
        argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : std::uint16_t{8080};
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        tinycoro::IoContext context;
        tinycoro::TcpListener listener(context, port);
        context.spawn(accept_connections(context, listener));

        std::cout << "C++20 coroutine HTTP echo server listening on 0.0.0.0:"
                  << listener.port() << '\n';
        context.run();
    } catch (const std::exception& error) {
        std::cerr << "HTTP echo server failed: " << error.what() << '\n';
        return 1;
    }
}
