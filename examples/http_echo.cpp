// HTTP echo server: returns a 200 response echoing the request body.
// Usage: ./http_echo [port]  (default: 8080)

#include "tinycoro/event_loop.h"
#include "tinycoro/tcp_server.h"
#include "tinycoro/http_parser.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unordered_map>

static tinycoro::EventLoop* g_loop = nullptr;

int main(int argc, char* argv[]) {
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 8080;

    tinycoro::EventLoop loop;
    g_loop = &loop;
    std::signal(SIGINT,  [](int) { if (g_loop) g_loop->stop(); });
    std::signal(SIGTERM, [](int) { if (g_loop) g_loop->stop(); });

    tinycoro::TcpServer server(&loop, port);

    // One parser per connection, stored by fd
    std::unordered_map<int, tinycoro::HttpParser> parsers;

    server.set_connection_callback([&parsers](tinycoro::TcpConnectionPtr conn) {
        parsers.emplace(conn->fd(), tinycoro::HttpParser{});
        std::printf("[+] HTTP connection fd=%d\n", conn->fd());
    });

    server.set_message_callback([&parsers](tinycoro::TcpConnectionPtr conn,
                                            tinycoro::Buffer& buf) {
        auto it = parsers.find(conn->fd());
        if (it == parsers.end()) return;

        auto result = it->second.parse(buf);
        if (result == tinycoro::HttpParser::COMPLETE) {
            const auto& req = it->second.request();
            // Build response
            std::string body = "method=" + req.method +
                               " path=" + req.path +
                               "\r\n" + req.body;
            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: keep-alive\r\n"
                "\r\n" + body;
            conn->send(resp);
            it->second.reset(); // ready for next request (keep-alive)
        } else if (result == tinycoro::HttpParser::ERROR) {
            conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
            conn->close();
        }
    });

    server.start();
    std::printf("HTTP echo server listening on port %u\n", port);
    loop.run();
    return 0;
}
