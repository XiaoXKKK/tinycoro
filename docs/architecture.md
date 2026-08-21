# Architecture

## Component boundary

The primary path is:

```text
Task<T> -> IoContext -> EventLoop/epoll -> TcpListener/TcpStream
```

`HttpParser` and `Buffer` sit above the transport. The callback
`TcpServer`/`TcpConnection` path shares `EventLoop` but does not participate in
`Task` scheduling. SPSC/MPMC queues and `ThreadPool` are independent concurrency
utilities; adding them to the same repository does not make fd state cross-thread.

## Coroutine-frame ownership

`Task<T>` is lazy: calling a coroutine creates a suspended frame and returns a
move-only owner.

- `IoContext::spawn(Task<void>)` transfers a root frame into the context. The
  context records the typed handle, sets a completion callback in the promise,
  and places the handle on the ready queue.
- `co_await` on an rvalue `Task<T>` transfers a child frame into the awaiter. The
  child promise stores its parent continuation. Final suspend performs symmetric
  transfer to that continuation; the awaiter destroys the child after
  `await_resume` consumes its result or exception.
- A completed root links itself into an intrusive completion list stored in its
  promise. `IoContext` drains that list after resumption, removes ownership,
  captures the first root exception, and destroys the frame.
- Destroying an `IoContext` first removes fd registrations and timers, then
  destroys any still-suspended root frames.

This keeps the ready queue non-owning: it contains `coroutine_handle` values, while
ownership lives in either the root map or a parent coroutine frame.

## Ready and waiting transitions

```text
spawn -> ready -> running
                  |
                  +-> yield -> ready
                  +-> EAGAIN -> fd waiter -> ready event -> ready
                  |                       -> deadline    -> ready
                  |                       -> close       -> ready
                  +-> final suspend -> completed -> destroy
```

`IoContext` is single-threaded. `spawn`, `run`, fd operations, and close
cancellation must be performed on its executor thread. There is intentionally no
eventfd-based cross-thread wakeup in the current scope.

## Readiness registration

Each fd has an `FdState` containing one optional reader, one optional writer, and
one `Channel` registered with epoll. Full duplex is supported, but two concurrent
readers or two concurrent writers on the same fd are rejected.

`EventLoop` uses edge-triggered epoll with `EPOLLRDHUP`. Transport operations
always attempt the syscall before awaiting readiness:

- `read_some` retries `recv` after `EINTR`; it returns data, EOF, a structured
  error, timeout, or close result.
- `write_all` advances an offset until all bytes are sent; `EAGAIN` suspends the
  task, so a slow peer applies backpressure to that connection task.
- `accept` loops around transient errors and constructs an accepted stream through
  an fd guard, closing the descriptor if object/control-block allocation fails.

## Deadlines and cancellation

A timed wait receives a monotonically increasing token and a steady-clock
deadline. Timer entries live in a min-heap. Readiness or close clears the current
wait slot; a later heap entry whose token no longer matches is a tombstone and is
discarded. This avoids arbitrary deletion from `priority_queue`.

`TcpStream::close` and `TcpListener::close` remove epoll interest, complete both
directional waiters with a false result, and enqueue their continuations. Stream
operations translate this to `IoStatus::Closed`; a suspended accept reports a
closed-listener logic error rather than a timeout.

## Error boundary

Nested task exceptions are stored in the child promise and rethrown from
`await_resume`. Unhandled root exceptions are rethrown by `IoContext::run` only
after the completed root has been removed and destroyed. Allocation or epoll
control failures propagate to the loop caller; registration paths roll back
partially created state.

## Backpressure and borrowed data

`write_all` does not own its `string_view`. The caller must keep the referenced
bytes alive until the task completes. This makes the memory boundary explicit and
avoids an implicit unbounded output buffer. Applications needing queued writes
must define a queue bound, overflow behavior, and completion policy above this
primitive.
