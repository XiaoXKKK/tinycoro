# Benchmark methodology

The repository intentionally publishes no QPS or latency comparison. Results
from an unspecified "4-core machine" or a one-off cloud VM are not reproducible
evidence.

## Included microbenchmark

`coroutine_switch_bench` measures scheduler round trips for one coroutine. One
round trip includes the scheduler resume, `swapcontext` into the coroutine,
`yield`, `swapcontext` back, and ready-queue bookkeeping. It must not be reported
as the cost of one machine-level context switch.

The executable prints CSV:

```text
run,round_trips,total_ns,ns_per_round_trip
```

Run an optimized build without sanitizers, pin it to one CPU where supported,
and retain every run rather than only the best sample.

```bash
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DTINYCORO_BUILD_BENCHMARKS=ON \
  -DTINYCORO_BUILD_TESTS=OFF
cmake --build build-bench --parallel
./build-bench/coroutine_switch_bench 1000000 10 > results.csv
```

## Before publishing a number

Record at least:

- CPU model, physical/logical cores, governor, turbo state, OS/kernel, compiler,
  flags, git commit, CPU affinity, and whether the host is virtualized;
- warm-up policy, number of iterations/runs, all raw samples, median and tail
  statistics, and the definition of one measured operation;
- for a network benchmark: client tool/version, connection count, payload,
  keep-alive policy, duration, server/client CPU placement, error count, and proof
  that the client is not the bottleneck;
- an equivalent baseline and identical environment before making a comparison.

Codespaces are suitable for functional verification, not for resume-grade
latency claims because the underlying VM and noisy-neighbor load are not fixed.