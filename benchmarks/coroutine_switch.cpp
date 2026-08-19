#include "tinycoro/scheduler.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

long long run_once(std::size_t round_trips) {
    tinycoro::Scheduler scheduler;
    std::size_t completed = 0;
    scheduler.spawn([&] {
        for (std::size_t i = 0; i < round_trips; ++i) {
            ++completed;
            tinycoro::Scheduler::yield_current();
        }
    });

    const auto begin = std::chrono::steady_clock::now();
    scheduler.run();
    const auto end = std::chrono::steady_clock::now();
    if (completed != round_trips)
        std::abort();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

} // namespace

int main(int argc, char* argv[]) {
    const std::size_t round_trips =
        argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 1000000;
    const int runs = argc > 2 ? std::atoi(argv[2]) : 5;
    if (round_trips == 0 || runs <= 0)
        return 2;

    (void)run_once(std::min<std::size_t>(round_trips, 10000));
    std::cout << "run,round_trips,total_ns,ns_per_round_trip\n";
    for (int run = 1; run <= runs; ++run) {
        const long long nanoseconds = run_once(round_trips);
        const double per_round_trip =
            static_cast<double>(nanoseconds) / static_cast<double>(round_trips);
        std::cout << run << ',' << round_trips << ',' << nanoseconds << ',' << std::fixed
                  << std::setprecision(2) << per_round_trip << '\n';
    }
}