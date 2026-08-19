# Design decisions

## Why stackful `ucontext`?

The project uses stackful coroutines to make suspension mechanics visible:
register state, an independent stack, caller/callee contexts, and the constraint
that the coroutine object cannot move after `makecontext` embeds its address.
This is educational rather than a production recommendation. `ucontext` was
removed from POSIX.1-2008.

A C++20 version would model readiness through awaiters and avoid a fixed stack
per connection. It would also require explicit lifetime handling for coroutine
frames and continuation ownership—different trade-offs worth discussing, not a
free replacement.

## Why N:1 instead of M:N?

The existing code has a thread pool and MPMC queue, but merely placing those
modules in one repository does not create an M:N runtime. The implemented main
path keeps an `IoContext` and all of its fd state on one OS thread. This makes
ownership and readiness deterministic and prevents overstating what the code
does.

An M:N extension would need per-worker pollers or a cross-thread wakeup fd,
work-stealing/affinity policy, migration rules for fd ownership, shutdown and
cancellation semantics, and tests for races across those boundaries.

## Why one waiter per fd direction?

Concurrent reads on one byte stream have ambiguous message ownership. Allowing
multiple readers would require a dispatch/fairness policy and make cancellation
harder. `IoContext` therefore accepts one reader and one writer and throws on a
duplicate direction. Full duplex remains possible.

## Why a tokenized timer heap?

Removing an arbitrary timer from `priority_queue` is expensive. Each wait gets a
unique token. Readiness clears the slot; later, the heap entry is recognized as
stale. The trade-off is temporary tombstones and potentially extra timer
wakeups, bounded by the number of completed timed waits.

## Why synchronous backpressure?

A common callback API queues every `send`, which needs a high-water mark,
overflow policy, and write-complete callbacks. The coroutine path instead keeps
one response in the task and waits for the socket to drain. This has a clear
memory bound per task but prevents that task from doing other work until the
peer catches up.

## Why keep the callback path?

`TcpConnection`/`TcpServer` provide a compact Reactor comparison for interviews:
callbacks make readiness explicit but spread one request across control-flow
edges; stackful tasks restore sequential control flow but hide suspension points
inside I/O methods. The two paths share `EventLoop` but are not mixed in one
connection.