# ADR-009: Queue topology {#adr_log_009}

| Field        | Value                                                             |
|--------------|-------------------------------------------------------------------|
| **Status**   | Accepted                                                          |
| **Date**     | 2026-11-18                                                        |
| **Deciders** | Cas Rovers                                                        |
| **Module**   | `logging` (`viscera::logging`)                                    |
| **Related**  | Depends on: ADR-001, ADR-008, ADR-011, ADR-014 · Supersedes: none |

## Context

ADR-001 leaves one shared structure between the producers and the backend thread. Everything else in the hot path is
thread-local. This ADR decides the shape of that structure.

The queue has to provide:

- **Many producers, one consumer.** Any engine thread may log. Exactly one backend thread formats and writes.
- **A per-thread order.** Messages from one thread appear in the order that thread emitted them. Across threads, the
  order is the order in which the queue accepted the records, not a chronological order by timestamp. ADR-001 already
  states that limit.
- **Bounded memory.** An unbounded queue turns a slow disk into unbounded RAM growth. The bound is what makes ADR-010
  necessary.
- **A short critical section.** The lock, if there is one, must cover the push or the index change and nothing else.
  Formatting and sink I/O stay outside it, or a slow console stalls every log call.
- **Defined argument lifetime.** ADR-008 constructs argument objects in the slot. The consumer cannot memcpy those
  bytes out as a blob and then reuse the slot. Freeing a slot has to run the destroy thunk first.
- **Room to practise the concurrency the module is for**, without a lock-free structure the engine does not yet need.

The slot layout and the "no allocation after startup" rule are ADR-011. What happens when every slot is full is
ADR-010. This ADR decides who shares which buffer, and how a record changes hands.

## Decision

**One bounded multi-producer, single-consumer ring, allocated when the logger starts and fixed for its lifetime,
guarded by a `std::mutex` and a `std::condition_variable`. The mutex is held only to publish a slot, to wait for a
slot, or to free a slot after its arguments have been destroyed. The backend formats from the occupied slot with the
mutex released, then destroys and frees the slot, then writes sinks. Argument bytes are never memcpy'd as a blob.**

Details:

1. **There is a single ring.** Every producer pushes to it. The backend is the only popper. Capacity is a power of two
   so the index can wrap with a mask. The default capacity is 8192 slots. `initialize` accepts a different capacity so
   tests can fill a tiny ring (ADR-016). The capacity does not change after `initialize` returns.

2. **A slot holds one record** as defined in ADR-011, or one control record for a sink change (ADR-013). Shutdown does
   not use a slot. It is `request_stop` on the backend `std::jthread` (ADR-014), so a full ring cannot postpone
   shutdown.

3. **The mutex is a `std::mutex`.** A producer locks, and either publishes a record into the next free slot or, when
   the ring is full, drops it under the rule in ADR-010. It then unlocks. A successful push calls `notify_one` on the
   condition variable. A drop does not. Publishing includes constructing the argument objects in that slot (ADR-008)
   before the slot is visible to the consumer.

4. **Wait uses `std::condition_variable` and a `std::stop_callback`, not a stop-token overload.**
   `std::condition_variable` has no `wait(lock, stop_token, pred)` in C++23; that overload exists only on
   `std::condition_variable_any`, whose interruptible wait may allocate. The backend instead waits with
   `cv.wait(lock, predicate)` where the predicate is "the ring is not empty, or stop has been requested, or abandon has
   been requested" (ADR-014). A `std::stop_callback` registered on the backend `jthread`'s stop token calls
   `cv.notify_all()`. `shutdown` also notifies after `request_stop` (ADR-014). Spurious wakes are harmless.

5. **Backend sequence for one log record**, mutex not held across format or I/O:

    1. **Wait.** Lock, wait until a slot is available or stop/abandon is requested, then unlock **without freeing the
       slot**. If abandon is set, the remaining occupied slots are destroyed and counted, then the thread exits
       (ADR-014). If stop is set and the ring is empty, the thread exits to flush. If a slot is available, the backend
       holds a pointer (or index) to that occupied slot.
    2. **Format.** Call the slot's format thunk into the backend buffer (ADR-011). The mutex is not held. The slot stays
       occupied, so a producer cannot overwrite it.
    3. **Destroy and free.** Lock, run the destroy thunk (or skip it when it is null), advance the read index so the
       slot is reusable, unlock.
    4. **I/O.** Write the formatted line to the sinks (ADR-013). The mutex is not held. Producers can reuse the freed
       slot while the backend is in a sink.

   Control records follow the same occupancy rule: the backend applies the sink change with the mutex released, then
   frees the slot. Overflow summaries (ADR-010) are written by the backend off-ring, between records, and do not occupy
   a slot.

6. **The queue mutex is never held across `Sink::write` or `Sink::flush`.** That is an invariant, not an optimisation.
   It is what keeps a slow console from stalling every producer, and what keeps the fatal console path (ADR-015) from
   deadlocking with the backend.

7. **Per-thread order follows from the mutex.** A thread's next log cannot enter the critical section until its
   previous push has published. Two threads are ordered by whichever acquires the mutex first. Timestamps may therefore
   disagree with dequeue order by a small amount. The backend does not sort.

8. **The ring storage is allocated once in `initialize` and freed after the backend has joined.** The hot path does not
   allocate slots, grow the ring, or allocate nodes. Slots still occupied at abandon are destroyed before the storage
   is freed (ADR-014).

## Alternatives considered

### Per-thread buffers drained by a collector

Each producer writes a thread-local SPSC buffer, and the backend merges them. Producers do not share a mutex, which is
the attractive case under heavy logging. ADR-001 deferred this here. It was not chosen because each thread then needs a
registered buffer, a scheme for threads that start and exit, and a merge rule that still has to define cross-thread
order. That is more lifetime machinery than the logger needs before a profile says the shared mutex is hot.

### A lock-free bounded MPSC ring

A Vyukov-style or similar queue would let producers push without a mutex. It is a better fit for the learning goal in
isolation. It was not chosen because the overflow, shutdown, and "I/O outside the lock" behaviour are easier to state
and to test with a mutex, and because a subtle race in a lock-free queue becomes lost or duplicated log lines. The
mutex critical section is a few stores. It is the right first version.

### An unbounded linked queue

Push never fails, so ADR-010 would not exist. It was not chosen because a stalled backend retains every record until
the process runs out of memory, and because every push allocates. Both conflict with ADR-001 and ADR-011.

### One queue per severity

A full trace queue could not block errors. It was not chosen because it multiplies the synchronisation, weakens the
single dequeue order ADR-001 relies on, and still needs a policy when the error queue itself is full. Severity is a
filter (ADR-007), not a second topology.

### Copy the slot out, then format the copy

Memcpy of the record into backend-local storage would free the slot before formatting and keep occupancy even shorter.
It was not chosen because ADR-008 argument objects are not a trivially copyable blob: copying them with `memcpy` and
reusing the slot without running the destroy thunk is undefined. Relocating via a third thunk is extra machinery for a
few hundred bytes of occupancy during format, which is CPU, not I/O. Format-from-slot, then destroy-and-free, then I/O
keeps the mutex off the sink path without inventing relocation.

### `std::condition_variable_any` with a stop-token wait

That is the overload that takes a `stop_token`. It was not chosen because the interruptible wait typically allocates
and can throw `std::bad_alloc` on the backend's idle path. A `stop_callback` that notifies a `std::condition_variable`
wakes shutdown without that allocation and without a ring slot.

## Consequences

### Positives

- One consumer and one ring preserve a single dequeue order, and sinks stay single-threaded (ADR-013).
- Memory for queued records is fixed at startup. A logging burst cannot grow the heap without limit.
- The critical section is short and does not include formatting or I/O, so a slow sink does not hold the producers'
  mutex.
- Argument objects live in the slot until the destroy thunk runs. The consumer does not memcpy them.
- The concurrency is visible and testable: one mutex, one condition variable, a `jthread` stop callback, a full ring.
- Tests can shrink the capacity and exercise overflow without generating thousands of lines (ADR-016).

### Negatives

- Every enabled log takes the same mutex. Under a burst from many threads the mutex is the contention point.
- A slot stays occupied during formatting. That is shorter than occupying it during sink I/O, and it is still one slot
  that cannot be reused until the format thunk returns.
- A control record shares the ring with log records, so a full ring can also refuse a sink change (ADR-013). Shutdown
  is deliberately not in that position.
- 8192 slots of the ADR-011 size is a few megabytes reserved for the life of the logger, even when logging is quiet.
- Cross-thread order is acquisition order, not timestamp order. Readers who expect a global sort will not get one.

### Revisit when

A profile shows the queue mutex on the frame path under a realistic log rate. The replacement to evaluate first is a
lock-free bounded MPSC ring with the same slot, capacity, overflow, and destroy-before-reuse rules, and after that the
per-thread buffers ADR-001 already named.
