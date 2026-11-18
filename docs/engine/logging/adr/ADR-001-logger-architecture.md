# ADR-001: Logger architecture

| Field        | Value                                                     |
|--------------|-----------------------------------------------------------|
| **Status**   | Accepted                                                  |
| **Date**     | 2026-11-18                                                |
| **Deciders** | Cas Rovers                                                |
| **Module**   | `logging` (`viscera::logging`)                            |
| **Related**  | Depends on: foundation ADR-001 (C++23) · Supersedes: none |

## Context

The engine is a real-time 3D game engine written in C++23. It runs a real-time frame loop, so every millisecond of a
frame matters, and logging is called from everywhere: the game loop, the renderer, audio, asset loading and worker
threads.

A logger in this setting has to satisfy requirements that pull against each other:

- **Low latency on the calling thread.** Logging must not stall the frame loop. File and console I/O can block for
  milliseconds, and formatting consumes CPU time and may require dynamic allocation, depending on the formatting
  destination and implementation.
- **Thread safety.** Many threads log concurrently, and their messages must not interleave or corrupt each other.
- **Ordering.** Messages from one thread must appear in the order that thread emitted them.
- **Shutdown delivery.** Messages successfully accepted by the logger should be processed and passed to their configured
  sinks during a normal shutdown. Sink flushing and persistence guarantees are defined separately.
- **Extensibility.** Output may go to the console, a file, an in-game console or a network target, and new destinations
  should not require changes to the call sites.
- **Learning goal.** The module should be a good vehicle for practising C++ concurrency and system design, without
  becoming more complicated than the engine needs.

Two questions shape everything else in the module: **which thread does the I/O**, and **which thread does the
formatting**. This ADR answers both. Queue shape, overflow behaviour, allocation and sinks are decided in later ADRs,
within the frame set here.

## Decision

**We will use an asynchronous logger in which producer threads capture a log record and hand it to a single backend
thread, and the backend thread performs formatting and all sink I/O.**

Details:

1. **Producers (any thread)** do the minimum: check the level and category filter, capture the raw record, and enqueue
   it. A record contains the severity, category, source location, timestamp, thread id, the format string and the
   *unformatted* arguments. Producers never perform sink I/O, **except the direct console write on every `Fatal` entry
   point** (ADR-015). Whether enqueueing can block under queue saturation is determined by the queue's overflow policy.
2. **Backend thread (exactly one)** dequeues records, formats them with `{fmt}` (ADR-002), and writes the result to the
   configured sinks (ADR-013). Because a single backend thread processes records sequentially, all
   sinks observe the same dequeue order. This provides a consistent global processing order, but not necessarily
   chronological ordering across producer threads. Ordering between concurrent producers is determined by the queue's
   synchronisation semantics.
3. **Formatting is deferred.** Arguments must be converted into an owned representation whose lifetime
   extends until backend processing completes. Capture constructs those objects in the slot and installs one format
   thunk and one destroy thunk for the argument pack (ADR-008, ADR-011). Non-owning views, references
   and pointers must not be retained unless their lifetime is explicitly
   guaranteed. Supported argument types and their capture semantics are defined in ADR-008.
4. **Filtering happens on the producer side**, before any capture work, so disabled messages cost close to nothing
   (ADR-007).
5. **The queue between producers and the backend** is the only shared state on the hot path. Its topology, capacity and
   overflow behaviour are decided in ADR-009 and ADR-010.
6. **Lifetime** of the backend thread, including start-up order and flush-on-shutdown, is decided in ADR-014.

Conceptually:

```
 game thread   ─┐
 render thread  ├─► filter ─► capture record ─► [ queue ] ─► backend thread ─► format ─► sinks
 worker threads ┘            (cheap, no I/O)                (single consumer)
```

## Alternatives considered

### Synchronous logger with a mutex

Each calling thread formats the message and writes to the sinks while holding a lock. This is the simplest design, it is
trivially ordered and messages are never pending in memory, which makes it the most crash-friendly. It was not chosen
because every log call pays for formatting and I/O on the calling thread, and the lock turns concurrent logging into a
serialization point. A single slow write (disk flush, full pipe, console redirection) would stall the frame loop.

### Asynchronous logger with formatting on the producer thread

Producers format the message into a string and enqueue the finished text; the backend only writes. This makes the queue
payload trivial and avoids restricting argument types, since nothing needs to outlive the call. It was not chosen
because formatting is a large part of the per-call cost and usually allocates, so it keeps the most expensive work on
the hot path. It also makes it harder to meet the allocation goals in ADR-011.

### Asynchronous logger with multiple backend threads

A pool of backend threads formats and writes in parallel. This could raise throughput under extreme load. It was not
chosen because it breaks the single global ordering, forces sinks to be thread-safe, and adds complexity for a
throughput level a game logger should not need to reach.

### Per-thread buffers drained by a collector

Each producer thread writes to its own thread-local buffer, and a collector merges them. This avoids contention on a
shared queue. It is a topology question inside the architecture chosen here rather than an alternative to it, so it is
evaluated in ADR-009.

### Third-party logging library (spdlog and similar)

Adopting an existing asynchronous logger would give a mature, tested solution immediately. It was not chosen because
building the logger is an explicit goal of the project as a learning exercise in C++23 and system design, and because
owning the module gives full control over allocation behaviour, engine-specific context such as frame numbers (ADR-012),
and crash handling.

## Consequences

### Positives

- Logging calls stay cheap on the calling thread: a filter check, a record capture and an enqueue, with no I/O and no
  formatting, except the direct console write on `Fatal` (ADR-015).
- A single consumer gives one global message order and sinks that do not need to be thread-safe.
- Formatting and I/O costs can be absorbed by a thread that is not on the frame's critical path.
- Sinks can be added or swapped without touching call sites.
- The structure gives clear, separable topics for the remaining ADRs: queue, overflow, allocation, sinks, lifetime and
  crash handling.

### Negatives

- Messages are pending in memory between enqueue and write, so they can be lost if the process crashes or is killed,
  unless ADR-015 mitigates this.
- Deferred formatting means arguments must be captured by value. Only types that are safe to copy or move to another
  thread may be logged, which makes the API less flexible (see ADR-008).
- The design has more moving parts than a synchronous logger: a thread, a queue and a lifetime protocol, each with its
  own failure modes.
- A single backend thread is a throughput ceiling. If producers outrun it, the queue fills and ADR-010 governs what is
  dropped or blocked.
- Log output can lag behind the program's real state, which makes live debugging with a debugger or breakpoints slightly
  less direct.

### Revisit when

Profiling shows the single backend thread cannot keep up with realistic load, or the engine needs logs to survive hard
crashes in ways the async design cannot provide.
