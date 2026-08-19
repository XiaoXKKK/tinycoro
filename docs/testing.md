# Testing and quality evidence

## Automated coverage

The test suite currently contains 39 cases covering:

- coroutine completion, repeated yield/resume, stack reuse, and exception
  propagation across the context trampoline;
- bounded SPSC and sequence-number MPMC queues under concurrent producers and
  consumers;
- buffer cursor bounds, compaction/growth behavior, fragmented HTTP input,
  pipelining, conflicting `Content-Length`, unsupported transfer encoding, and
  request/body limits;
- `IoContext` FIFO scheduling, readable wakeup, deadline expiry, cancellation,
  a forced 1 MiB partial-write/backpressure path, and a real TCP loopback echo;
- coroutine pool bounds and thread-pool shutdown/drain behavior.

`gtest_discover_tests` registers every case separately with CTest.

## CI matrix

| Platform | Compiler/configuration | Purpose |
| --- | --- | --- |
| Ubuntu | GCC Debug + ASan/UBSan + `-Werror` | memory/UB checks and strict warnings |
| Ubuntu | Clang Release + `-Werror` | second compiler and optimized build |

ASan documents limited support for `makecontext`/`swapcontext`; CI disables
stack-use-after-return instrumentation but retains AddressSanitizer and
UndefinedBehaviorSanitizer for the surrounding C++ code.

## Local commands

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTINYCORO_ENABLE_SANITIZERS=ON \
  -DTINYCORO_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ASAN_OPTIONS=detect_stack_use_after_return=0 \
  ctest --test-dir build --output-on-failure
```

A Release smoke check should start `coro_http_echo`, send a POST with `curl`,
and verify the exact response shown in the root README.

## Important gaps

Passing this suite does not prove production readiness. Missing evidence includes
fuzzing, ThreadSanitizer coverage, long-running connection churn, fd exhaustion,
TLS/protocol conformance, cross-thread cancellation, slowloris behavior, and a
fixed-hardware network benchmark.