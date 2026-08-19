#include "tinycoro/buffer.h"
#include "tinycoro/io_context.h"
#include "tinycoro/tcp_stream.h"
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
volatile std::sig_atomic_t stop_requested = 0;

void on_signal(int) {
    stop_requested = 1;
}

void serve_echo(const std::shared_ptr<tinycoro::TcpStream>& stream) {
    tinycoro::Buffer input;
    for (;;) {
        const auto read = stream->read_some(input);
        if (!read)
            return;
        const std::string_view chunk(input.read_ptr(), read.bytes);
        const auto write = stream->write_all(chunk);
        input.consume(read.bytes);
        if (!write)
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
            context.spawn([connection] { serve_echo(connection); });
        }
        context.stop();
    });

    std::cout << "Coroutine echo server listening on 0.0.0.0:" << listener.port() << '\n';
    context.run();
}