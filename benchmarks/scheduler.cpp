#include "tinycoro/io_context.h"
#include "tinycoro/task.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

tinycoro::Task<void> yield_repeatedly(tinycoro::IoContext& context, std::size_t iterations,
                                      std::size_t& completed) {
    for (std::size_t i = 0; i < iterations; ++i) {
        ++completed;
        co_await context.yield();
    }
}

long long run_once(std::size_t iterations) {
    tinycoro::IoContext context;
    std::size_t completed = 0;
    context.spawn(yield_repeatedly(context, iterations, completed));

    const auto begin = std::chrono::steady_clock::now();
    context.run();
    const auto end = std::chrono::steady_clock::now();
    if (completed != iterations)
        std::abort();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

} // namespace

int main(int argc, char* argv[]) {
    const std::size_t iterations =
        argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 1'000'000;
    const int runs = argc > 2 ? std::atoi(argv[2]) : 5;
    if (iterations == 0 || runs <= 0)
        return 2;

    (void)run_once(std::min<std::size_t>(iterations, 10'000));
    std::cout << "run,iterations,total_ns,ns_per_schedule\n";
    for (int run = 1; run <= runs; ++run) {
        const long long nanoseconds = run_once(iterations);
        const double per_schedule =
            static_cast<double>(nanoseconds) / static_cast<double>(iterations);
        std::cout << run << ',' << iterations << ',' << nanoseconds << ',' << std::fixed
                  << std::setprecision(2) << per_schedule << '\n';
    }
}
