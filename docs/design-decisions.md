# Design decisions

## One coroutine model

The repository uses C++20 stackless coroutine frames as its only coroutine
implementation. `Task<T>`, `promise_type`, `coroutine_handle`, and explicit
awaiters form the complete execution path; there is no second scheduler or
reusable-stack abstraction behind the API.

## Lazy, move-only tasks

Lazy start makes ownership transfer visible: a task either remains in a local
`Task` object, moves into an awaiting expression, or moves into
`IoContext::spawn`. Copying and lvalue `co_await` are disabled so that two owners
cannot destroy the same frame.

Root spawning is limited to `Task<void>` because detached result ownership would
otherwise be ambiguous. Nested `Task<T>` values remain structured under a parent
coroutine.

## Single-threaded fd ownership

All coroutine frames and fd wait state in an `IoContext` stay on one executor
thread. This avoids hidden task migration, fd-affinity races, and a cross-thread
cancellation protocol. Scaling across cores is an application-level choice: run
independent contexts with explicit connection distribution.

The separate `ThreadPool` is for bounded CPU/general tasks. It does not resume
`IoContext` coroutines.

## One waiter per fd direction

Concurrent reads on one byte stream make message ownership ambiguous, and
concurrent writes need an ordering policy. The context therefore accepts one
reader and one writer per fd. Full duplex remains possible, while duplicate
directional waits fail immediately.

## Edge-triggered readiness

Transport methods attempt `accept4`, `recv`, or `send` before registering a wait
and retry after every wakeup. This prevents readiness notifications from becoming
the source of truth: the syscall result remains authoritative. Terminal epoll
events wake both applicable directions so EOF and socket errors are observed by
the retry.

## Tokenized deadline heap

Removing arbitrary entries from a binary heap would require an index or linear
search. Each wait instead receives a token. Completion clears the slot, and stale
timer entries are ignored later. The trade-off is temporary tombstones bounded by
the number of completed timed waits not yet popped.

## Synchronous write backpressure

`write_all` keeps one caller-owned payload and suspends on a full kernel send
buffer. It does not enqueue unlimited responses. This bounds hidden library
memory, but the connection task cannot produce its next response until the peer
drains the current one.

## Descriptor adoption before allocation

`accept4` returns a raw descriptor before a `TcpStream` and its `shared_ptr`
control block are allocated. The descriptor is therefore placed in a small RAII
guard first. Either allocation failure closes it; successful construction
transfers ownership exactly once.

## Queue and thread-pool boundaries

`SPSCQueue` is a power-of-two ring for exactly one producer and one consumer; one
slot is reserved. `MPMCQueue` uses per-slot sequence numbers and exposes a fixed
capacity. Both preallocate their slots and use lock-free `size_t` atomics.

`ThreadPool` blocks idle `std::jthread` workers on a counting semaphore. A
lifecycle mutex serializes submission with shutdown, so the pool is bounded and
blocking when idle but is not advertised as fully lock-free. Submitted task
exceptions are retained for the caller.

## Why retain the callback Reactor

Callbacks remain a practical event-driven interface. The callback server is kept
as a small comparison for ownership and control flow; it is clearly named, uses
the same checked epoll wrapper, and is not an alternate path inside the coroutine
API.
