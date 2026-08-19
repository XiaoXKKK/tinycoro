#include "tinycoro/buffer.h"
#include "tinycoro/http_parser.h"
#include "tinycoro/io_context.h"
#include "tinycoro/tcp_stream.h"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
volatile std::sig_atomic_t stop_requested = 0;

void on_signal(int) {
    stop_requested = 1;
}

bool send_bad_request(const std::shared_ptr<tinycoro::TcpStream>& stream) {
    constexpr std::string_view response = "HTTP/1.1 400 Bad Request\r\n"
                                          "Content-Length: 0\r\n"
                                          "Connection: close\r\n\r\n";
    return static_cast<bool>(stream->write_all(response));
}

void serve_http(const std::shared_ptr<tinycoro::TcpStream>& stream) {
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
            if (!stream->write_all(response))
                return;
            parser.reset();
            continue; // parse any pipelined request already in the buffer
        }
        if (parsed == tinycoro::HttpParser::ERROR) {
            (void)send_bad_request(stream);
            return;
        }

        const auto read = stream->read_some(input, 16 * 1024, std::chrono::seconds{30});
        if (!read)
            return;
    }
}
} // namespace

int main(int argc, char* argv[]) {
    const auto port =
        argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : std::uint16_t{8080};
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    tinycoro::IoContext context;
    tinycoro::TcpListener listener(context, port);

    context.spawn([&] {
        while (!stop_requested) {
            auto connection = listener.accept(std::chrono::milliseconds{250});
            if (!connection)
                continue;
            connection->set_nodelay(true);
            context.spawn([connection] { serve_http(connection); });
        }
        context.stop();
    });

    std::cout << "Coroutine HTTP echo server listening on 0.0.0.0:" << listener.port() << '\n';
    context.run();
}