# ADR-008: Custom type formatting {#adr_log_008}

| Field        | Value                                                                      |
|--------------|----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                   |
| **Date**     | 2026-11-18                                                                 |
| **Deciders** | Cas Rovers                                                                 |
| **Module**   | `logging` (`viscera::logging`)                                             |
| **Related**  | Depends on: ADR-001, ADR-002, ADR-003, ADR-004, ADR-011 · Supersedes: none |

## Context

ADR-001 defers formatting to the backend thread. The record therefore has to carry the arguments, not references to
whatever happened to be in the caller's stack frame. ADR-004 makes a missing formatter a compile error. ADR-002 says
custom types specialise `fmt::formatter<T>`. This ADR decides which types may be logged, how they are stored, and what
a formatter is allowed to do.

That decision is also an allocation decision. A `std::string` member copied into the record will heap-allocate if the
logger stores a real `std::string`. ADR-011 forbids allocation on the producer hot path, so the set of loggable types
and the way they are copied have to agree with that budget.

Failures follow ADR-003. A copy that throws, or a record that cannot be captured, is dropped and counted. The caller
does not hear about it. A formatter that throws is caught on the backend, one record at a time.

Constraints:

- **The record owns its arguments.** Views, references, and pointers into the caller's memory must not be what the
  backend later reads, unless the target is static storage (format strings and source locations, ADR-004 and ADR-005).
- **Capture does not allocate.** The inline budget that makes this true is set in ADR-011. This ADR defines the
  representation that has to fit in it.
- **The queue is homogeneous.** The backend does not know the argument types of a given call. Something stored with the
  record has to know how to format those bytes, and how to end their lifetime.
- **Argument objects are not a memcpy blob.** A nothrow copy is not the same as trivial copyability. Placement into
  the inline buffer must start a real object of type `T`, and that object must be destroyed before the slot is reused
  (ADR-009, ADR-011).
- **Formatters run on the backend thread**, possibly long after the call returned, and possibly while the caller holds
  locks the formatter must not try to take.
- **The logging module does not include subsystem headers.** A formatter for a renderer type lives with that type, not
  in the logger.

## Decision

**Only types the logger can copy into the record without allocating may be logged. Built-in types and strings are
copied by value into the record. Any other type opts in with `fmt::formatter<T>` beside that type, under a contract
that the formatter does not throw, does not allocate on the producer's behalf, and reads only the copied value. Capture
installs one format thunk and one destroy thunk for the whole argument pack; it does not memcpy argument bytes as a
slot blob.**

Details:

1. **The producer stores decayed arguments in the record's inline buffer, not in a `std::string` and not in
   `fmt::format_args`.** `fmt::format_args` holds references, which is the lifetime bug this ADR exists to prevent.
   For each enabled call, the call site instantiates **one format thunk** and **one destroy thunk** that know the full
   `Args...` pack. The record stores those two function pointers and the constructed argument bytes. The backend calls
   the format thunk, then the destroy thunk (ADR-009). The queue never sees the C++ type.

   Illustratively (the exact signatures are an implementation choice, as long as they match ADR-011):

   ```cpp
   using FormatThunk  = void (*)(fmt::memory_buffer& out, const Record& record);
   using DestroyThunk = void (*)(Record& record); // no-op, or null, when the payload is trivially destructible
   ```

   The format thunk reconstructs typed references to the objects in the inline buffer and formats them with `{fmt}`
   against the stored format-string pointer. On the backend that call is a **runtime** format (`fmt::vformat` or
   `fmt::runtime`): the public API's compile-time check (ADR-004) has already run at the call site, and the backend
   does not have the original `Args...` in its signature. The destroy thunk runs `std::destroy_at` (or equivalent) on
   each constructed argument, in reverse order of construction, then returns. A null destroy thunk means the payload
   is trivially destructible and needs no run.

2. **Construction into the buffer starts object lifetime.** The producer places each argument with `std::construct_at`
   (or an equivalent nothrow construct) into aligned storage inside the 256-byte buffer. Header scalars (level,
   category, timestamps, pointers, flags) are trivially copyable and are not argument objects. **Argument bytes are
   not memcpy'd as a blob**, either on capture or when the backend later claims the slot (ADR-009).

3. **Types the logger knows how to copy:**
    - arithmetic types, `bool`, and enumerations (formatted as the underlying integer unless a formatter specialises the
      enum);
    - `std::chrono::duration` specialisations, copied as the duration object;
    - string literals, `std::string_view`, `std::string`, and `const char*` / `char*`, stored as the **characters**
      copied into the inline buffer together with an explicit length. They are not stored as `std::string` objects and
      not stored as pointers into the caller's memory. A null `const char*` / `char*` is stored as the fixed token
      `(null)`, pointing at static storage;
    - object pointers and function pointers, stored as the address and formatted as an address. They are never
      dereferenced. `char*` is the string case above, not this case.

4. **Bounded C-string copy.** For `const char*` and `char*` that are not null, the producer copies characters into the
   remaining inline budget and **only if a NUL is found in that window**. It does not call unbounded `strlen`, and it
   does not read past the remaining budget. If no NUL appears in the remaining bytes, the whole record is dropped and
   the argument-budget counter is incremented (ADR-011). There is no truncated C-string record.

5. **A user-defined type is loggable when all of the following hold.** It has an `fmt::formatter<T>` specialisation. A
   copy or move into the record is `noexcept` and does not allocate. The value's bytes, plus alignment padding and the
   small per-argument header the capture uses, fit in the space ADR-011 leaves for arguments. The formatter's `format`
   reads only that value, does not throw, and does not call back into the logger.

6. **Anything else is rejected or dropped.** A missing formatter is a compile error (ADR-004). A copy that can throw is
   also a compile error: the capture requires a nothrow copy or move into the record. A copy that allocates is outside
   the supported set too, but allocation is not a type trait the compiler can see, so it is a contract on the formatter
   and the type, enforced in review and in tests rather than by a `static_assert`. If the bytes do not fit the inline
   budget, the whole record is dropped and counted (ADR-011). There is no partial record.

7. **`std::string` is not stored as a `std::string`.** Doing so would allocate whenever the text exceeds the
   small-string
   buffer. The characters are copied into storage the record already owns. The same rule covers `std::string_view` and
   C strings. A caller who builds a `std::string` before the call pays for that allocation themselves; the logger does
   not add another one.

8. **Formatters live next to the type they format**, in that type's header, included where the type is logged. That
   header includes `{fmt}` in order to specialise `fmt::formatter<T>`. Call sites that only log built-in types include
   the logging headers and do not include `<fmt/...>` themselves (ADR-002). The logging module does not gain a
   dependency on renderer, audio, or game types. No formatter is required for a type until something logs it.

9. **The backend is the only thread that runs `format` on queued records.** By then the producer has returned, so the
   formatter must not touch thread-local state of the caller, locks the caller might hold, or memory the copied value
   does not own. Formatters that need a name from another system copy that name into the value at the call site, before
   capture. The direct `fatal` console path (ADR-015) is the sole producer-side exception: it formats into a stack
   buffer on the caller, then still enqueues owned arguments for the other sinks.

10. **A throwing formatter is a broken contract, not a supported failure mode.** ADR-003's per-record catch still turns
    it into a diagnostic line that keeps the original level, category, timestamp, and source location, and drops the
    user arguments. The destroy thunk still runs for that record, so argument lifetime does not leak when formatting
    fails.

## Alternatives considered

### Format on the producer and store a finished string

The queue then holds text, and every type `{fmt}` accepts is loggable. ADR-001 already rejected this, because the
expensive work and the allocation stay on the calling thread.

### Heap-allocate an owned copy of any formattable type

`std::string`, `std::vector`, and arbitrary structures would just work, with the copy living until the backend finishes.
It was not chosen because it makes every non-trivial log an allocation on the frame path, which ADR-011 exists to
avoid. A later overflow path for oversized arguments can add a heap fallback without making it the normal case.

### Trivially copyable types only

The capture becomes `memcpy`, which is easy to prove non-allocating and non-throwing. It was not chosen because it bans
small engine types that are still safe to copy and that do not allocate, as soon as they grow a user-provided copy
constructor. The contract in point 5 is the actual requirement: nothrow, no allocation, bounded size. The format and
destroy thunks exist so that requirement is implementable in C++ without treating the slot as a trivially copyable blob.

### A logger-owned registry of formatters

Types register themselves at startup, and the record stores a type id. This supports plugins and types the logger does
not know at compile time. It was not chosen because the engine's types are known at compile time, a registry is shared
mutable state on the hot path, and ADR-004 already requires the formatter to be visible at the call.

### One function pointer per argument

A per-argument formatter cannot reconstruct a single `fmt::format(fmt, args...)` against the original format string.
The backend needs the full pack in one thunk. Per-argument destroyers are possible, but one destroy thunk that knows
the pack is fewer pointers in the header and matches the format thunk.

## Consequences

### Positives

- Deferred formatting cannot dangle into a returned caller's stack frame. The backend reads objects the record owns.
- Object lifetime in the inline buffer is defined: `construct_at` on the producer, destroy thunk before slot reuse.
- The hot path can log the types call sites actually use (numbers, enums, short strings, pointers) without allocating.
- Custom types opt in beside themselves, so the logging module stays free of subsystem headers.
- A missing formatter is a compile error at the call site, not a backend diagnostic months later.
- One format thunk keeps the queue homogeneous while `{fmt}` still does the formatting, including multi-argument
  strings.

### Negatives

- A long string or a large structure that does not fit the inline budget is dropped whole. The caller is not told,
  except through the counter ADR-011 defines.
- Types that allocate on copy, such as a `std::filesystem::path` or a container, are not loggable until someone writes a
  bounded, non-allocating capture for them. Logging `path.string()` allocates in the caller, which is outside the
  logger and is still the wrong thing on a hot path.
- Pointers print as addresses. A call that meant "the object this points at" will look correct and log the wrong thing.
- Every logged custom type pays for a formatter and for a capture instantiation at the call site, which is more compile
  time (ADR-004).
- The formatter contract is not fully enforceable by the compiler. A formatter that throws or logs is caught or called
  out in review, not rejected by the type system. Tests cover the throwing case (ADR-016).
- An unterminated `const char*` that does not contain a NUL within the remaining budget is dropped rather than copied.
  That is the memory-safe choice; it can surprise a caller who expected truncation.

### Revisit when

Real call sites regularly exceed the inline budget with legitimate arguments, or an engine type that allocates on copy
needs to be logged from the frame path. The response is a bounded heap fallback for those arguments, not unbounded
capture of every type.
