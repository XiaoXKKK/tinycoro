# Benchmark methodology

The repository intentionally publishes no QPS, latency, or comparison claim.
Results from unspecified hardware or a one-off cloud VM are not reproducible
evidence.

## Included microbenchmark

`scheduler_bench` measures repeated cooperative scheduling through
`IoContext::yield()`. One reported iteration includes:

1. resuming the root coroutine from the ready queue;
2. executing the loop body and constructing the yield awaiter;
3. enqueueing the same `coroutine_handle` and suspending.

The final completion/resume cost is included in the run total but amortized over
all requested iterations. The result is not a machine context-switch cost, a
standalone `coroutine_handle::resume` cost, or a network measurement.

The executable prints:

```text
run,iterations,total_ns,ns_per_schedule
```

Build without sanitizers, pin to one CPU where supported, and retain every sample:

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTINYCORO_BUILD_BENCHMARKS=ON -DTINYCORO_BUILD_TESTS=OFF
cmake --build build-bench --parallel
taskset -c 2 ./build-bench/scheduler_bench 1000000 10 > results.csv
```

## Before publishing a number

Record at least:

- CPU model, physical/logical cores, governor, turbo state, OS/kernel, compiler,
  flags, git commit, CPU affinity, and virtualization status;
- warm-up policy, iterations, all raw runs, median/tail statistics, and the exact
  definition of one measured operation;
- for a network test: client/version, connections, request/response bytes,
  keep-alive policy, duration, CPU placement, errors, and evidence that the client
  is not the bottleneck;
- an equivalent baseline built and run under the same conditions.

Codespaces are suitable for functional verification, not resume-grade performance
claims, because VM hardware and neighboring load are not fixed.
