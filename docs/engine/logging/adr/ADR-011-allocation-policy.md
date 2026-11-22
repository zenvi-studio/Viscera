# ADR-011: Allocation policy {#adr_log_011}

| Field        | Value                                                                      |
|--------------|----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                   |
| **Date**     | 2026-11-18                                                                 |
| **Deciders** | Cas Rovers                                                                 |
| **Module**   | `logging` (`viscera::logging`)                                             |
| **Related**  | Depends on: ADR-001, ADR-004, ADR-005, ADR-008, ADR-009 · Supersedes: none |

## Context

Formatting and I/O are off the caller (ADR-001), and the ring is fixed (ADR-009). The remaining way for a log call to
disturb the frame is to allocate: a `std::string` for the line, a node for the queue, a copy of an argument that
outgrows the small-string buffer.

Allocation on the caller is worse here than its cycle cost. It takes a lock in the allocator, it perturbs the frame's
memory traffic, and it can fail. ADR-003 then has to drop the record anyway. The policy should make the failure mode
"does not fit", which is predictable, rather than "the heap said no", which depends on the rest of the process.

The backend is a different budget. It runs off the frame, one record at a time. A reusable buffer there can grow
occasionally without becoming part of the hot path. This ADR is about the producer, and it says what the backend is
allowed to do so the two are not confused later.

What is stored is decided in ADR-008. This ADR decides the memory that storage has to fit in, and which pointers live
in the slot header so the backend can format and destroy without knowing `Args...`.

## Decision

**After `initialize` returns, an enabled log call does not allocate. Each ring slot is a fixed header plus 256 bytes of
inline argument storage, reserved with the ring. Arguments that do not fit are dropped and counted. The header holds
one format thunk and one destroy thunk for the whole argument pack. Argument bytes are constructed in place; they are
not a memcpy blob. The backend formats into one reusable buffer.**

Details:

1. **The hot path is the producer work of an enabled, compiled-in log call:** filter, construct arguments into a slot,
   publish the slot. That work uses the slot and the stack. It does not call `operator new`, `malloc`, or anything that
   does. A disabled call returns before this, and also does not allocate.

2. **The ring's slots are allocated in `initialize` and freed after the backend joins.** Capacity times slot size is the
   standing cost, paid once (ADR-009). There is no per-record heap block and no growth of the ring.

3. **A slot is a header plus an inline argument buffer of 256 bytes.** The header holds:

    - the level and category;
    - the timestamp, thread id, and frame number (ADR-012);
    - source-location pointers and line (ADR-005);
    - the format-string pointer (ADR-004);
    - the **format thunk** and the **destroy thunk** from ADR-008 (the destroy thunk may be null when the payload is
      trivially destructible);
    - the flags a later ADR needs (for example the console-already-written flag in ADR-015);
    - whatever small per-argument metadata the capture needs (counts, sizes), if it is not stored in the 256 bytes.

   The exact field order and padding are an implementation choice. The 256 bytes are argument objects and string
   characters only; the header is extra. Header scalars are trivially copyable. **The argument region is not memcpy'd
   as a blob** on capture, on pop, or on free (ADR-008, ADR-009).

4. **Format strings and source-location strings are not copied.** They are pointers to static storage, as ADR-004 and
   ADR-005 require. They do not consume the 256 bytes.

5. **Argument bytes do consume it.** Numbers, durations, pointer values, characters of strings, alignment padding, and
   the small per-argument header the capture writes all count. Strings are raw characters in this buffer, not
   `std::string` objects (ADR-008). If the next argument would pass 256 bytes, or a C string has no NUL in the remaining
   budget, the producer publishes nothing, increments the argument-budget counter, and returns. The record is all or
   nothing. Call sites cannot assume a 256-character string will fit: the ceiling is payload plus headers plus padding.

6. **The budget counter is separate from the overflow counter** (ADR-010) **and from the not-running counter**
   (ADR-014). Tests and the end-of-run snapshot can tell a full ring from an oversized call from a log while stopped.
   All three are producer-side drops in the sense ADR-003 describes.

7. **The backend keeps one format buffer, reused across records.** It starts at 4 KiB. If a formatted line does not fit,
   the backend grows that buffer and keeps the new size. Growth happens on the backend thread, outside the queue mutex,
   and is not a producer allocation. The buffer is freed after the backend joins. Formatting uses the slot's format
   thunk with this buffer as the destination, while the slot is still occupied (ADR-009).

8. **Startup and shutdown may allocate.** Creating the ring, the format buffer, and the sinks is `initialize`'s job.
   Destroying them is `shutdown`'s job. Neither runs on the logging hot path.

## Alternatives considered

### Allocate a record on every enabled call

A `make_unique` per line is simple and places no limit on argument size. It was not chosen because it puts the
allocator on every log, including traces on the frame thread, and because allocation failure becomes a normal drop
path.

### A growable per-record side buffer, used only when 256 bytes is not enough

The common case stays inline, and a long string still gets through via the heap. It was not chosen for the first
version because the hot path then has a branch that allocates, and "no allocation" stops being a property tests can
hold the module to. ADR-008 already names this as the revisit when real call sites need it.

### Format into a caller-provided buffer on the producer

The caller passes a `std::span<char>`, and the queue stores finished text. This also avoids a hidden allocation, and
`{fmt}` supports it. It was not chosen because it moves formatting back onto the producer, which ADR-001 rejected, and
because every call site would have to supply a buffer.

### A process-wide pool of argument blocks, larger than the slot

Oversized arguments would take a pooled block instead of calling the general allocator. It was not chosen because it is
a second allocator to reason about, and the engine has not shown that 256 bytes is too small. The fixed slot is the
whole policy until a measurement says otherwise.

### Treat the whole slot as trivially copyable and memcpy it

That would let the backend copy the record out in one go. It was not chosen because it conflicts with ADR-008's
nothrow, non-trivial argument types. The header is trivial; the argument region is constructed and destroyed through
the thunks.

## Consequences

### Positives

- An enabled log on the frame path does not take the allocator lock and does not fail based on heap pressure.
- The memory used by queued records is fixed and known after startup: capacity times slot size.
- Format strings and locations stay free to store, which keeps the inline budget for the values that actually vary.
- Oversized calls fail predictably, one counter, no partial line.
- The backend can format without allocating per record in the steady state. A single long line grows the buffer once.
- Format and destroy are the same two pointers on every slot, so the ring stays homogeneous.

### Negatives

- 256 bytes is a hard ceiling for the arguments of one record. A long path, a wide matrix, or many strings in one call
  are dropped even when the heap is healthy and the ring is empty.
- The ceiling counts capture overhead and alignment, not only the programmer-visible values, so the usable space is a
  bit under 256. Call sites cannot assume a 256-character string will fit.
- 8192 slots with a header plus 256 bytes is a few megabytes held for the logger's lifetime, whether or not they are
  used.
- The backend may allocate when a line exceeds 4 KiB, and that allocation can fail. The backend treats that as a
  formatting failure for that record (ADR-003): count it, write the fixed diagnostic, run the destroy thunk, continue.
  It does not propagate to the caller, who has already returned.
- Proving "no allocation" continuously needs a test seam. ADR-016 states what is checked now and what is left for a
  later injection point.

### Revisit when

Call sites that the engine wants regularly exceed the 256-byte budget, or the standing ring reservation is too large for
the process. The first change to consider is a rare heap fallback for oversized arguments, with the inline path left as
the one the frame thread is written against.
