# ADR-012: Timestamps and frame context

| Field        | Value                                                                      |
|--------------|----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                   |
| **Date**     | 2026-11-18                                                                 |
| **Deciders** | Cas Rovers                                                                 |
| **Module**   | `logging` (`viscera::logging`)                                             |
| **Related**  | Depends on: ADR-001, ADR-005, ADR-007, ADR-011, ADR-013 · Supersedes: none |

## Context

A log line without time, thread, or frame is hard to place. The same message from the renderer and from a worker, in
the same millisecond of two different frames, is two events. ADR-001's record already lists a timestamp and a thread
id. The frame number is the engine-specific piece a general logging library will not have, and it is one of the reasons
ADR-001 gave for owning the logger.

Where those values are captured matters. The backend runs later, and under load it runs noticeably later. A timestamp
taken when the line is formatted describes the logger, not the call. The frame number has the same problem: by the time
the backend runs, the game may be several frames ahead. Both have to be sampled on the producer, into the record, before
the call returns.

They also have to be cheap and self-contained. The logging module cannot call into a frame loop it does not own, and a
log call should not take a system-clock syscall if a monotonic clock will do.

Constraints:

- **Sample on the producer, after the filter and before enqueue**, only for messages that are actually kept.
- **The sample must not allocate and must not take the queue mutex** before it is needed. Clock and thread id are
  thread-local reads.
- **The logger must not depend on the game loop.** The loop publishes a number. The logger stores it.
- **Values must be safe to read on the backend** after the caller has returned, which they are if they are integers in
  the record (ADR-011).
- **Cross-thread order is still the queue's order** (ADR-001, ADR-009). Timestamps explain the lines. They are not a
  sort key.

## Decision

**Each kept record stores a monotonic timestamp taken at the call, a 32-bit operating-system thread id, and the latest
frame number published to the logger. The backend prints elapsed time and an estimated wall time from those integers.**

Details:

1. **The timestamp is nanoseconds of `std::chrono::steady_clock` since a base sampled in `initialize`**, stored as
   `std::uint64_t`. It is monotonic for the life of that session. The producer does not read `system_clock` on the log
   path.
2. **Wall time is estimated on the backend** as the wall clock sampled once in `initialize`, plus the elapsed
   nanoseconds on the record. The printed line can show both elapsed time and that estimate. The estimate drifts if the
   system clock is stepped. It is an aid for reading the file next to an external log, not a second source of ordering.
   A later `initialize` after `shutdown` samples a new base.
3. **The thread id is a `std::uint32_t` from the operating system**, not `std::this_thread::get_id()` stored as a
   pointer-sized object. Windows uses `GetCurrentThreadId`. Linux uses `gettid`. Other platforms use their equivalent
   numeric thread identifier. The value is cached in thread-local storage on first use by that thread, so later logs do
   not repeat the query. A platform that only offers a wider handle hashes it once into the cached `uint32_t`. A
   collision would mislabel a thread in the text. It would not corrupt the queue.
4. **The frame number is a `std::uint64_t` atomic, published by `setFrame` and copied into the record with a relaxed
   load.** The game loop calls `setFrame` once per frame with the frame it is starting. Until the first call, the value
   is 0. The logger does not know what a frame is beyond that integer, and it does not include a frame-loop header.
5. **Ordering of the frame snapshot is per thread.** A `setFrame` that happens-before a log on the same thread is
   visible to that log. Another thread may still observe the previous frame for a short time. That is the same relaxed
   rule as the level filter (ADR-007). The frame number is context, not a lock.
6. **Disabled calls do not sample.** The filter returns first (ADR-007). A compiled-out severity does not contain the
   sample either.
7. **The backend prints these fields. It does not recapture them.** The text pattern is specified in ADR-013. The
   function name from the source location is stored with the record (ADR-005) and is omitted from the default pattern
   because those names are long and implementation-defined. The data stays in the slot so a later pattern can show it.

## Alternatives considered

### `system_clock` on every call

The line would carry a true wall time, which lines up with external tools without an estimate. It was not chosen
because the system clock can jump backwards, and because reading it is a heavier call than the steady clock on the
platforms this engine cares about. One sample at startup keeps the per-call read monotonic and small.

### Timestamp on the backend

The producer would store less, and the clock would be read once per record on a thread that is allowed to be slow. It
was not chosen because the stamp would describe dequeue time. Under a full ring and a slow sink, that is far from the
call, and the frame number would be wrong for the same reason.

### The logger reads a frame counter owned by the engine loop

No `setFrame`, and the number cannot be forgotten. It was not chosen because it would make the logging module depend on
the loop, which inverts the dependency the foundation layout asks for and recreates the cycle ADR-003 avoids. A missed
`setFrame` shows up as a stuck frame number, which is obvious in the log.

### Omit the frame until the loop exists

The field would be added later, when a frame loop exists. It was not chosen because the record layout and the printed
pattern would change under existing call sites and tests. A zero until the loop publishes is a cheap reservation.

## Consequences

### Positives

- Time and frame describe the call, not the moment the backend happened to wake up.
- The producer stores three integers. No allocation, no system-clock read, no dependency on the game loop.
- Thread ids in the file match the ids debuggers and operating-system tools show.
- Elapsed time stays monotonic even when the wall clock is adjusted. The wall-time estimate is still there for a human
  reading the file.
- Tests can set the frame, publish two values, and assert the record captured the one that was current (ADR-016).

### Negatives

- The wall time on a line is an estimate. A clock step mid-run makes it disagree with an external log by that step.
- `setFrame` is a convention. If the loop forgets it, every line says frame 0 or a stale frame, and the logger cannot
  detect that.
- A relaxed frame load can attach a log on a worker thread to the previous frame.
- Thread-id caching assumes the numeric id is stable for the life of the thread. A platform that recycles ids while a
  thread-local cache still holds the old one would mislabel lines. The supported platforms do not do that for a living
  thread.
- A hashed id, on a platform that needs the hash, can collide. The text would show two threads as one.

### Revisit when

The wall-time estimate is misleading in practice, or a subsystem needs context besides the frame (a tick, a job name, a
streaming region). Add a field the producer copies in, the way the frame is copied in, rather than letting the backend
query that subsystem.
