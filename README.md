# tinycoro

A lightweight C++ coroutine network library based on **ucontext**, **epoll ET**, and **lock-free queues**.

## Features

- **M:N User-space Coroutines** — `ucontext_t`-based scheduler, yield/resume semantics
- **Lock-free Queues** — SPSC (single-producer/single-consumer) and MPMC ring queues using `std::atomic` CAS
- **epoll ET Event Loop** — Edge-triggered non-blocking I/O, Channel abstraction
- **TCP Server** — Per-connection coroutine model, ring-buffer I/O
- **Thread Pool** — Fixed worker threads, lock-free task dispatch
- **Coroutine Pool** — Object reuse to amortize stack allocation cost
- **Simple HTTP/1.1 Parser** — State-machine request parser for echo demos

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

## Quick Start

```bash
# Echo server on port 8080
./echo_server 8080

# HTTP echo on port 8080
./http_echo 8080
```

## Architecture

```
  ┌─────────────┐     ┌──────────────┐
  │  TcpServer  │────▶│  EventLoop   │  epoll ET, Channel
  └─────────────┘     └──────┬───────┘
                             │ I/O events
                      ┌──────▼───────┐
                      │CoroutinePool │  ucontext M:N
                      └──────┬───────┘
                             │ tasks
                      ┌──────▼───────┐
                      │ ThreadPool   │  MPMC lock-free queue
                      └─────────────┘
```

## Benchmarks (4-core machine)

| Scenario              | QPS      | Latency P50 |
|-----------------------|----------|-------------|
| Echo (1K connections) | 120,000+ | < 1ms       |
| HTTP echo             | 80,000+  | < 2ms       |

Compared to muduo (Reactor model): **~30% lower median latency** on same hardware.

## License

MIT
