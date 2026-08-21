#include "tinycoro/buffer.h"
#include "tinycoro/io_context.h"
#include "tinycoro/task.h"
#include "tinycoro/tcp_stream.h"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

namespace {
volatile std::sig_atomic_t stop_requested = 0;

void on_signal(int) {
    stop_requested = 1;
}

tinycoro::Task<void> serve_echo(std::shared_ptr<tinycoro::TcpStream> stream) {
    tinycoro::Buffer input;
    for (;;) {
        const auto read = co_await stream->read_some(input);
        if (!read)
            co_return;

        const std::string_view chunk(input.read_ptr(), read.bytes);
        const auto write = co_await stream->write_all(chunk);
        input.consume(read.bytes);
        if (!write)
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
        context.spawn(serve_echo(std::move(connection)));
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

        std::cout << "C++20 coroutine echo server listening on 0.0.0.0:" << listener.port()
                  << '\n';
        context.run();
    } catch (const std::exception& error) {
        std::cerr << "echo server failed: " << error.what() << '\n';
        return 1;
    }
}
