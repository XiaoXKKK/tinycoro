# Testing and quality evidence

## Automated coverage

The current suite contains 44 GoogleTest cases, each registered separately with
CTest:

- 7 task/runtime cases: lazy start, nested values, FIFO yield scheduling, root
  and nested exception propagation, empty-task rejection, and destruction of
  suspended frames;
- 7 buffer cases for cursor bounds, compaction/growth, and direct-write commit;
- 12 HTTP parser cases covering fragmentation, pipelining, strict request lines,
  framing ambiguity, transfer-encoding rejection, and exact configured limits;
- 8 I/O cases covering readable resumption, simultaneous read/write waiters,
  deadlines, a forced 1 MiB partial-write path, real loopback accept/echo, and
  close cancellation for listeners and streams;
- 5 thread-pool cases covering invalid construction, shutdown rejection, drain,
  concurrent submission with exactly-once IDs, and exception retention;
- 5 queue cases, including ordered SPSC delivery and MPMC exactly-once validation
  for every produced ID rather than a checksum-only assertion.

Passing these tests supports the implemented ownership and state transitions; it
does not prove production readiness.

## CI matrix

| Platform | Compiler/configuration | Purpose |
| --- | --- | --- |
| Ubuntu | GCC Debug + ASan/UBSan + `-Werror` | memory/UB checks and strict warnings |
| Ubuntu | Clang Release + `-Werror` | second compiler and optimized build |

The GCC job also performs a clean, non-sanitized install and builds a standalone
C++20 consumer through `find_package(tinycoro CONFIG REQUIRED)`. That consumer
creates an `IoContext`, spawns a `Task<void>`, and runs it, so package verification
exercises compiled runtime symbols rather than a header-only helper.

## Local commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTINYCORO_ENABLE_SANITIZERS=ON -DTINYCORO_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For the installed-package check:

```bash
cmake -S . -B package-build -DCMAKE_BUILD_TYPE=Release -DTINYCORO_BUILD_EXAMPLES=OFF -DTINYCORO_BUILD_TESTS=OFF
cmake --build package-build --parallel
cmake --install package-build --prefix /tmp/tinycoro-install
cmake -S tests/package_consumer -B package-consumer -DCMAKE_PREFIX_PATH=/tmp/tinycoro-install
cmake --build package-consumer --parallel
./package-consumer/package_consumer
```

A Release smoke test should start `http_echo`, issue the POST shown in the root
README, verify the exact response body, and then stop the server.

## Important gaps

Current evidence does not include fuzzing, ThreadSanitizer, long-running connection
churn, fd exhaustion/fault injection, slowloris limits, TLS/protocol conformance,
cross-thread context wakeup, or a fixed-hardware network benchmark. The HTTP
parser intentionally does not implement chunked transfer decoding.
