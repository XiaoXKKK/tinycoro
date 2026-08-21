#include "tinycoro/event_loop.h"
#include "tinycoro/http_parser.h"
#include "tinycoro/tcp_server.h"
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace {
tinycoro::EventLoop* event_loop = nullptr;
}

int main(int argc, char* argv[]) {
    const auto port =
        argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : std::uint16_t{8080};
    tinycoro::EventLoop loop;
    event_loop = &loop;
    std::signal(SIGINT, [](int) {
        if (event_loop)
            event_loop->stop();
    });
    std::signal(SIGTERM, [](int) {
        if (event_loop)
            event_loop->stop();
    });

    tinycoro::TcpServer server(&loop, port);
    std::unordered_map<int, tinycoro::HttpParser> parsers;

    server.set_connection_callback([&](tinycoro::TcpConnectionPtr connection) {
        parsers.emplace(connection->fd(), tinycoro::HttpParser{});
    });
    server.set_close_callback(
        [&](tinycoro::TcpConnectionPtr connection) { parsers.erase(connection->fd()); });
    server.set_message_callback(
        [&](tinycoro::TcpConnectionPtr connection, tinycoro::Buffer& buffer) {
            auto iterator = parsers.find(connection->fd());
            if (iterator == parsers.end())
                return;
            const auto result = iterator->second.parse(buffer);
            if (result == tinycoro::HttpParser::COMPLETE) {
                const auto& request = iterator->second.request();
                const std::string body =
                    "method=" + request.method + " path=" + request.path + "\n" + request.body;
                const std::string response =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\nConnection: keep-alive\r\n\r\n" + body;
                connection->send(response);
                iterator->second.reset();
            } else if (result == tinycoro::HttpParser::ERROR) {
                connection->send("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                connection->close();
            }
        });

    server.start();
    std::printf("Callback HTTP echo server listening on port %u\n", port);
    loop.run();
}