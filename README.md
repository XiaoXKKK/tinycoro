# tinycoro

[![CI](https://github.com/XiaoXKKK/tinycoro/actions/workflows/ci.yml/badge.svg)](https://github.com/XiaoXKKK/tinycoro/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](https://kernel.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

`tinycoro` is a compact Linux networking runtime built around C++20 stackless
coroutines and edge-triggered epoll. The main path is a real `co_await` chain:
connection tasks call nonblocking TCP operations, suspend on `EAGAIN`, and resume
when the fd becomes ready or a deadline expires.

The repository is intentionally small and explicit. It is not presented as a
production networking library, a cross-thread coroutine runtime, or evidence for
any QPS or latency claim.

## What is implemented

- A lazy, move-only `Task<T>` with `promise_type`, nested continuations, value and
  exception propagation, and deterministic coroutine-frame ownership.
- A single-threaded `IoContext` that owns root `Task<void>` frames, schedules
  `coroutine_handle` objects in FIFO order, and integrates readiness with epoll.
- One read waiter and one write waiter per fd, tokenized deadline cancellation,
  explicit close cancellation, and cleanup of suspended frames on context
  destruction.
- RAII `TcpListener` and `TcpStream`. Accepted descriptors enter an ownership
  guard before allocation; partial writes suspend instead of accumulating an
  unbounded user-space output queue.
- Coroutine echo and HTTP/1.1 examples. The HTTP path supports incremental
  `Content-Length` parsing, keep-alive, pipelined buffered requests, and configured
  request/header/body limits.
- Bounded SPSC and sequence-number MPMC queues, plus an independent
  `std::jthread`/`counting_semaphore` thread pool. These utilities are not part of
  the `IoContext` execution path.
- A separate callback Reactor example built on the same epoll wrapper. It remains
  as a practical control-flow comparison and is not layered into the coroutine
  runtime.
- An installable `tinycoro::tinycoro` CMake target verified by a standalone C++20
  consumer that creates and runs a coroutine task.

## Main execution path

```text
connection Task<void>
  -> TcpStream::read_some / write_all
  -> syscall succeeds: continue in the coroutine frame
  -> EAGAIN: register fd waiter and suspend
  -> epoll readiness, close cancellation, or deadline
  -> enqueue coroutine_handle in IoContext
  -> resume and retry the syscall
```

See [architecture](docs/architecture.md) and
[design decisions](docs/design-decisions.md) for the ownership and state
boundaries.

## Build and verify

Requirements: Linux, a C++20 compiler, CMake 3.20+, and pthreads.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTINYCORO_ENABLE_SANITIZERS=ON -DTINYCORO_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For a clean Release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DTINYCORO_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

CI runs GCC with ASan/UBSan and Clang in Release mode. The current suite contains
47 independently registered tests; [testing notes](docs/testing.md) describe what
they cover and what remains unproven.

## HTTP smoke test

Start the coroutine HTTP server:

```bash
./build-release/http_echo 8080
```

In another terminal:

```bash
curl --http1.1 -sS -X POST --data 'hello' http://127.0.0.1:8080/demo
```

Expected body:

```text
method=POST path=/demo
hello
```

## Microbenchmark policy

`scheduler_bench` measures one cooperative `IoContext::yield()` scheduling cycle.
It does not measure network latency or a machine context switch.

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTINYCORO_BUILD_BENCHMARKS=ON -DTINYCORO_BUILD_TESTS=OFF
cmake --build build-bench --parallel
./build-bench/scheduler_bench 1000000 5
```

No benchmark result is published without fixed hardware, raw samples, workload
details, and an equivalent baseline. See [benchmark methodology](docs/benchmark.md).

## Deliberate limits

- Linux/epoll only; `IoContext` has no cross-thread wakeup or task migration.
- No TLS, asynchronous DNS/connect API, HTTP chunked decoding, or protocol-level
  flow-control policy.
- Closing an owned socket cancels its fd waiters, but there is no general
  cancellation-token tree.
- `TcpStream::write_all` borrows its `string_view` until the returned task
  completes.
- The callback server and thread pool are independent utilities, not hidden
  alternate coroutine schedulers.
