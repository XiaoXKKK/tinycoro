// Echo server: echoes back every received message.
// Usage: ./echo_server [port]  (default: 8080)

#include "tinycoro/event_loop.h"
#include "tinycoro/tcp_server.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>

static tinycoro::EventLoop* g_loop = nullptr;

int main(int argc, char* argv[]) {
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 8080;

    tinycoro::EventLoop loop;
    g_loop = &loop;

    // Graceful shutdown on SIGINT/SIGTERM
    std::signal(SIGINT,  [](int) { if (g_loop) g_loop->stop(); });
    std::signal(SIGTERM, [](int) { if (g_loop) g_loop->stop(); });

    tinycoro::TcpServer server(&loop, port);

    server.set_connection_callback([](tinycoro::TcpConnectionPtr conn) {
        std::printf("[+] connection fd=%d\n", conn->fd());
    });

    server.set_message_callback([](tinycoro::TcpConnectionPtr conn,
                                   tinycoro::Buffer& buf) {
        // Echo everything back
        std::string data = buf.retrieve_all_as_string();
        conn->send(data);
    });

    server.start();
    std::printf("Echo server listening on port %u\n", port);
    loop.run();
    return 0;
}
