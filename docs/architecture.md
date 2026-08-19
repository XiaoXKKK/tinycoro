# Architecture

## Scope

The primary path is a single-threaded, N:1, cooperative I/O runtime. It connects
`Coroutine`, `IoContext`, `EventLoop`, and `TcpStream`; the thread pool and bounded
queues remain independent exercises and are not presented as an M:N scheduler.

## Ownership

- `IoContext` owns every live `Coroutine` in a task table keyed by a monotonic ID.
- The ready queue stores IDs, not owning pointers, so duplicate/stale wakeups can
  be ignored by checking the task state.
- `TcpStream` owns its fd and calls `IoContext::cancel(fd)` before closing it.
- An fd state is heap allocated because `EventLoop` retains a stable `Channel*`
  while the unordered map may rehash.
- Connection tasks capture `shared_ptr<TcpStream>` because C++17 `std::function`
  requires copyable callables and the stream must live across suspension.

## Task state machine

```text
spawn
  |
  v
Ready --resume--> Running --normal return--> destroyed
  ^                 |
  |                 +--yield()-------------------+
  |                 |                            |
  |                 +--EAGAIN--> Waiting         |
  |                                 |            |
  +-------- readiness/timeout/cancel-+------------+
```

A task may have only one outstanding suspension. An fd may have one reader and
one writer, which matches the single-owner `TcpStream` model and rejects
ambiguous concurrent reads early.

## Readiness and edge-triggered I/O

`EventLoop` uses `EPOLLET | EPOLLRDHUP` on Linux. Socket operations always attempt
the syscall before registering interest. After
`EAGAIN`, the current task is stored in the fd's read or write wait slot and its
coroutine yields. EOF/HUP/error events wake both relevant directions so the next
syscall can return the authoritative result.

Callback removal during dispatch is safe: after a read callback, the event loop
checks that the same channel is still registered before invoking a write
callback. This avoids calling through a connection destroyed by the read path.

## Deadlines

Each timed wait receives a monotonic token and a `steady_clock` deadline. Timer
entries are kept in a min-heap. A readiness event clears the wait slot but leaves
its timer entry as a cheap tombstone; when the entry reaches the heap top, its
token is compared with the current slot and stale entries are discarded. This
avoids an indexed heap or O(n) cancellation.

## Backpressure

`TcpStream::write_all` retains the caller-owned payload on the suspended
coroutine stack. A partial `send` advances an offset; `EAGAIN` parks the task
until writable readiness. The API does not append to an unbounded per-connection
write buffer, so a slow peer naturally stops that connection task from
producing the next response.

The callback `TcpConnection` path is intentionally different: it has a write
buffer and exists as a Reactor comparison. The coroutine examples use
`TcpStream`.

## Exception boundary

Exceptions must not unwind across a C `makecontext` trampoline. `Coroutine::entry`
captures `exception_ptr`, switches back to the caller, and `resume()` rethrows on
the normal C++ stack. `IoContext` erases the failed task before propagating the
exception from `run()`.

The callback Reactor comparison is also fail-fast: internal setup errors and
exceptions from user callbacks propagate to the `EventLoop::poll`/`run` caller
after owned descriptors are cleaned up. Applications that choose the callback
API define their own logging, retry, or shutdown policy at that loop boundary.
