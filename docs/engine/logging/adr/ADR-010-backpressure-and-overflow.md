# ADR-010: Backpressure and overflow

| Field        | Value                                                                      |
|--------------|----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                   |
| **Date**     | 2026-11-18                                                                 |
| **Deciders** | Cas Rovers                                                                 |
| **Module**   | `logging` (`viscera::logging`)                                             |
| **Related**  | Depends on: ADR-001, ADR-003, ADR-009, ADR-013, ADR-015 · Supersedes: none |

## Context

ADR-009 bounds the ring. When producers enqueue faster than the backend can format and write, the ring fills. The
question is what the next producer does.

Waiting is the usual queue answer, and it is the wrong one on a frame thread. A full ring means the backend is already
behind, often because a sink is slow. Blocking the producer couples the frame to that sink, which is the failure
ADR-001 moved I/O off the caller to avoid. Dropping a record is a loss of diagnostics. ADR-003 requires that loss to be
counted rather than silent, and it leaves the overflow counter to this ADR.

The same choice applies to every severity. A `Fatal` line is more important than a `Trace` line, but a special case
that waits, or that discards a queued `Trace` to make room, is extra policy on the hot path and is easy to get wrong
under the mutex.

Constraints:

- **Producers do not wait for capacity.** They may wait for the short mutex critical section in ADR-009. They do not
  sleep, spin, or block until a slot frees.
- **The caller still gets `void` and no exception** (ADR-003). Overflow is not a status code at the call site.
- **Records already queued stay queued.** The earlier lines of a burst are usually the ones that explain it. Dropping
  the oldest to make room would erase that context and complicate the ring.
- **A drop must remain visible** even if the ring stays full for a long time. A summary line that itself needs a free
  slot cannot be emitted in the state it is describing.
- **`Fatal` uses the logging path.** Crash-time delivery of the fatal line is ADR-015, not a reason to block here.

## Decision

**When the ring is full, the producer drops the new record, increments a counter, and returns. It does not wait for
space. The backend later writes one summary line directly to the sinks, not through the ring.**

Details:

1. **The drop happens inside the queue critical section**, after the producer has observed a full ring. The record is
   not published. No condition-variable notify runs for a drop.
2. **The counter is a `std::uint64_t` atomic**, incremented for each dropped record. It counts only ring overflow:
   dropped log records **and dropped control records** (late `addSink`, ADR-013). The summary line reports that combined
   count as dropped "records". A record dropped because the inline argument budget was exceeded is a different counter
   (ADR-011). A record dropped because the logger is not running is a different counter (ADR-014). The snapshot ADR-003
   describes is this set, plus formatting failures, per-sink write failures, and flush-timeout.
3. **`Fatal` is not a special case inside the queue.** A full ring drops a fatal record the same way. ADR-015 gives
   every `Fatal` entry point a direct console write so the line is not only dependent on a free slot.
4. **The summary is produced by the backend, between records, when the overflow counter has moved.** The backend
   formats a fixed line, with no user arguments, reporting how many records were dropped since the previous summary,
   and writes it to the enabled sinks. It does not push that line back onto the ring, so a full ring cannot prevent the
   report forever. The summary is a logger diagnostic: it bypasses the level and category filter so a raised minimum
   cannot hide it. It is passed to sinks as `Warning`, so `ConsoleSink` colors the whole line with the warning color
   (ADR-013). That level does not put the summary through the filter.
5. **The summary is coalesced.** A burst of a thousand drops produces one line with the count, not a thousand lines.
   The counter itself keeps the total for the process lifetime; the summary carries the delta.
6. **Already queued records are left in order.** The implementation does not scan the ring for a low-severity record to
   evict, and it does not overwrite the oldest slot.

## Alternatives considered

### Block the producer until a slot is free

The ring becomes a normal bounded buffer, and nothing is dropped. It was not chosen because the wait includes sink
latency. A stalled file sink would stall every thread that logs, including the frame thread. That recreates the
synchronous logger ADR-001 rejected, one full queue later.

### Block only non-realtime threads, or only below `Error`

Callers would declare themselves realtime, or high severity would wait. It was not chosen because the logger cannot
tell a frame thread from a loader thread by looking at the call, and a severity-based wait is a hidden priority
inversion inside the mutex. One policy for every producer is the one that stays obvious.

### Drop the oldest record to make room

The new line always wins, so the freshest state is kept. It was not chosen because the start of a burst is often the
failure, and the tail is the flood. Evicting the oldest also means the consumer and the producer are modifying the read
side of the ring, which fights the simple push/pop in ADR-009.

### Drop the new record with no summary

The counter alone satisfies "counted, not hidden" for tests that read the snapshot. It was not chosen as the whole
policy because a person reading the log file cannot see the snapshot. A single summary line makes the hole visible in
the same place as the messages. The counter remains the source of truth for tests.

## Consequences

### Positives

- A full queue cannot stall the frame loop or any other producer. The extra wait is at most the mutex, and only while
  another thread pushes or pops.
- The beginning of a burst stays in the ring. What is lost is the overflow, and the summary says how much.
- The summary cannot be lost to the same overflow it is reporting, because it is written by the backend.
- `Fatal` does not grow a second queue policy. Its extra delivery guarantee is isolated in ADR-015.
- Tests can observe overflow precisely by reading the counter and the summary line (ADR-016).

### Negatives

- Under a sustained overrun, new messages are discarded for as long as the backend stays behind. The log goes quiet
  except for the summary, which can look like the engine hung when in fact the queue is full.
- Callers are not told at the call site. A system that logged an error may believe it was recorded.
- High-severity lines are dropped with the same rule as traces, apart from the direct console path for `fatal`.
- The summary is written as soon as the backend notices, which may be after later records that did fit have already
  been written. The gap and the summary are not perfectly adjacent.
- A `uint64_t` counter will not wrap in practice. If it ever did, the snapshot and the delta would lie. That case is
  ignored.

### Revisit when

A real incident is explained only by a line that overflow dropped, or a slow sink makes sustained loss normal. The
responses to weigh then are a larger ring, a sink that keeps up, or a reserved slot for `Error` and `Fatal`, not
blocking the producer.
