# tinycoro

[![CI](https://github.com/XiaoXKKK/tinycoro/actions/workflows/ci.yml/badge.svg)](https://github.com/XiaoXKKK/tinycoro/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

`tinycoro` is a compact C++17 learning project that connects stackful coroutines
to a real non-blocking TCP path. A connection task issues ordinary-looking
`read_some`/`write_all` calls; on `EAGAIN`, the runtime parks that coroutine and
resumes it only after epoll reports readiness or a deadline expires.

The project is intentionally small enough to explain in an interview. It is not
presented as a production networking library or as an M:N runtime.

## What is implemented

- `ucontext` stackful coroutines with explicit yield/resume, reusable 128 KiB
  heap-allocated stacks, and exception propagation across the context boundary.
- A single-threaded `IoContext` with ready/waiting task states, one read and one
  write waiter per fd, and deadline management through a tokenized min-heap.
- Edge-triggered epoll on Linux, including EOF/error wakeups and checked control
  operations.
- RAII `TcpListener`/`TcpStream`; partial writes suspend instead of growing an
  unbounded user-space output queue, providing synchronous backpressure.
- Coroutine echo and HTTP/1.1 examples. The incremental HTTP parser supports
  keep-alive/pipelined `Content-Length` requests with configurable size limits.
- Bounded SPSC/MPMC queues, a fixed thread pool, and a coroutine pool as separate
  concurrency exercises; they are not part of the network runtime's main path.
- An installable `tinycoro::tinycoro` CMake target, verified by a standalone
  `find_package` consumer project.
- 39 GoogleTest cases plus GCC ASan/UBSan and Clang Release CI jobs on Linux.

## Main execution path

```text
connection coroutine
  -> TcpStream::read_some / write_all
  -> syscall succeeds: continue on the same stack
  -> EAGAIN: IoContext records the fd waiter and yields
  -> epoll readiness (or deadline)
  -> task moves back to the ready queue and resumes
```

See [architecture](docs/architecture.md) and
[design decisions](docs/design-decisions.md) for ownership, state transitions,
ET semantics, timer invalidation, and backpressure details.

## Build and verify

Requirements: Linux, a C++17 compiler, CMake 3.16+, and pthreads.

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTINYCORO_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ASAN_OPTIONS=detect_stack_use_after_return=0 \
  ctest --test-dir build --output-on-failure
```

For a warning-clean Release build:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTINYCORO_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
```

## Run the coroutine HTTP example

```bash
./build-release/coro_http_echo 8080
curl -X POST --data 'payload' http://127.0.0.1:8080/demo
```

Expected body:

```text
method=POST path=/demo
payload
```

`echo_server`/`http_echo` retain the callback Reactor API as a comparison;
`coro_echo_server`/`coro_http_echo` exercise the coroutine-integrated path.

## Optional context-switch microbenchmark

```bash
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DTINYCORO_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel
./build-bench/coroutine_switch_bench 1000000 5
```

The tool emits CSV. No QPS or latency claim is checked into this repository;
[benchmark methodology](docs/benchmark.md) explains what must be recorded before
publishing a result.

## Known limitations

- The runtime is N:1 and cooperative. A blocking syscall or CPU-heavy task blocks
  every connection on that `IoContext` thread.
- `ucontext` was removed from POSIX.1-2008. It is used to expose stack/context
  mechanics; a production implementation should prefer maintained
  context-switching code or C++20 stackless coroutines.
- `IoContext` has no cross-thread wakeup, work stealing, cancellation token, TLS,
  or signal-safe shutdown primitive.
- The HTTP parser is deliberately incomplete: no chunked transfer encoding,
  trailers, upgrades, TLS, or full RFC validation.
- The bounded MPMC queue is a sequence-number exercise; the project does not make
  a formal wait-free progress guarantee for the thread pool.

More detail is in [testing](docs/testing.md). Contributions and bug reports are
welcome, but correctness evidence takes priority over feature count.