# ADR-003: No-throw error handling in logging calls {#adr_log_003}

| Field        | Value                                                                                                                                                       |
|--------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                                                                                                    |
| **Date**     | 2026-11-18                                                                                                                                                  |
| **Deciders** | Cas Rovers                                                                                                                                                  |
| **Module**   | `logging` (`viscera::logging`)                                                                                                                              |
| **Related**  | Depends on: ADR-001, ADR-002, ADR-010, ADR-011, ADR-014 · Supersedes: none · See also: [engine/error ADR-005](../../error/adr/ADR-005-logging-ownership.md) |

## Context

This ADR covers one narrow question: **what happens when something goes wrong during a logging call or while the logger
processes a record.** It does not define how the engine handles errors in general. The engine's general error handling
is a separate feature with its own design, and this ADR neither depends on it nor constrains it.

Logging is unusual among engine facilities. It is called from everywhere, including error paths, cleanup code,
destructors and code that is already handling a failure. A log call that can itself fail, or throw, is a hazard in
exactly the places where logging is most needed.

Failures can occur at several points in the architecture from ADR-001:

- **On the producer thread:** arguments may not fit the inline budget (ADR-011); the queue can be full (ADR-010); the
  logger may not be running (ADR-014). Capture itself is specified not to allocate (ADR-011).
- **On the backend thread, while formatting:** `{fmt}` can throw on a malformed format string that was not caught at
  compile time (ADR-004), and a custom `fmt::formatter` (ADR-008) can throw or misbehave.
- **On the backend thread, while writing:** a sink can fail, for example because a file cannot be opened, the disk is
  full, or a pipe is closed (ADR-013).

Constraints that shape the choice:

- **The caller can do nothing useful about a logging failure.** A game system that logged a warning cannot sensibly
  react to the warning not being written, and checking a result at every call site would clutter the code and be skipped
  in practice.
- **Logging calls must be safe anywhere**, including `noexcept` functions, destructors and code running during stack
  unwinding, where an escaping exception means `std::terminate`.
- **The logger must not take the engine down.** Diagnostics should not be the cause of a crash.
- **Silent loss is also a problem.** If failures are swallowed with no trace, a missing log line is indistinguishable
  from "nothing happened", which defeats the purpose of logging.
- **The logging module must be self-contained.** It cannot rely on facilities that themselves depend on logging.

## Decision

**Logging calls never throw and never return an error to the caller. All failures are contained inside the logging
module and recorded in internal counters.**

Details:

1. **The public logging API is `noexcept` and returns `void`.** There is no status code, no `std::expected` and no
   exception for the caller to handle.
2. **The producer path is written not to throw.** Operations that can fail (such as arguments that do not fit the
   inline budget) are guarded. If a record cannot be captured, it is dropped and counted, and the call returns normally.
   Because `noexcept` turns an escaping exception into `std::terminate`, the producer path must either be provably
   non-throwing or catch and absorb anything that can be thrown. `std::mutex::lock` may theoretically throw
   `std::system_error`; that failure is absorbed on the producer (drop and count) or treated as logger-internal, never
   as a public exception.
3. **The backend thread catches all exceptions at record granularity.** An exception during formatting a record affects
   only that record. The backend replaces it with a short fixed diagnostic record that keeps the original level,
   category, timestamp and source location but carries no user arguments, then continues with the next record.
4. **Sink failures are isolated per sink.** A failing sink does not stop other sinks, and does not stop the backend. The
   failure is counted for that sink. What a sink does after a failure (retry, disable itself, reopen) belongs to
   ADR-013.
5. **Failures are counted, not hidden.** The counters are readable through the logger as a snapshot so tests, tools and
   an end-of-run summary can show that something was lost. Producer-side drops are **three** counters, not one:

    - **overflow** — ring full, including dropped control records (ADR-010);
    - **argument-budget** — the inline 256-byte payload did not fit, or a C string had no NUL in the remaining budget
      (ADR-011);
    - **not-running** — log before `initialize` or after `shutdown` has begun (ADR-014).

   Plus **formatting failures** (ADR-003 point 3), **write failures per sink** (ADR-013), and **flush-timeout**
   (ADR-014). Allocation-failure injection is not a v1 test; the hot path is specified not to allocate (ADR-011,
   ADR-016).
6. **Custom formatters are expected not to throw.** This is a contract in ADR-008, not something the logger relies on.
   The backend's catch-all in point 3 is the safety net when the contract is broken.
7. **Programmer errors are a separate category.** Misusing the logger API (for example logging before initialization, or
   after shutdown) is not a runtime failure to absorb quietly. Its handling is defined in ADR-014.

## Alternatives considered

### Throw exceptions on failure

A failing log call throws, and callers may catch it. This is the conventional C++ way to report failure and makes
failures visible. It was not chosen because callers cannot recover from a logging failure, because logging would become
unsafe in destructors, `noexcept` functions and unwinding, and because a failing diagnostic would be able to terminate
the program.

### Return a status or `std::expected` from every log call

Each call returns whether it succeeded. This makes failure explicit without exceptions. It was not chosen because
callers would have to check or discard the result at every call site, which in practice means ignoring it, and it
burdens a facility that should be as light to call as possible. It also cannot report most failures, since formatting
and writing happen later on the backend thread, long after the call has returned.

### Fail fast (assert or terminate on failure)

Treat any logging failure as fatal. This makes problems impossible to miss during development. It was not chosen because
a full disk or a closed console should not kill the game, and because it makes the logger a source of crashes.

### Swallow failures silently

Catch everything and carry on, with no counters. This is the simplest option and gives the same caller-facing behaviour
as the decision. It was not chosen because failures become invisible: a missing message cannot be told apart from a
message never sent, and tests could not assert that nothing was lost.

### Report logger failures through the engine's general error handling

Surface logging failures through whatever mechanism the engine uses for errors in general. It was not chosen because it
would couple the logging module to a system that is designed separately and will itself want to log, creating a
dependency cycle, and because it would make a logging failure travel through code paths the logger cannot control.
Integration can be added later as an optional hook on top of the counters, without changing this decision.

## Consequences

### Positives

- Logging is safe to call from anywhere, including destructors, `noexcept` functions and failure handling code.
- Call sites stay simple: no error checking, no `try`/`catch`, no result to ignore.
- One bad record, one bad formatter or one failing sink cannot stop logging for the rest of the engine.
- Failures are observable through the counters, so tests, benchmarks (ADR-016) and tools can detect loss.
- The module has no dependency on the engine's general error handling.

### Negatives

- Callers are never told that a message was lost. Anyone who cares must look at the counters.
- The backend's catch-all can hide bugs in custom formatters unless the counters and diagnostic records are actually
  watched.
- The "must never throw" guarantee needs discipline on the producer path and is easy to break with an innocent-looking
  change. It needs tests (ADR-016). Allocation-failure injection waits until a capture path that actually allocates
  exists (the heap-fallback revisit in ADR-008).
- Catching at record granularity costs a little on the backend, and assumes exceptions are enabled for the logging
  module's build configuration.
- Failures that happen after a record leaves the producer (formatting and sink errors) appear only as a diagnostic line
  or a counter, which is less direct than an error the caller sees.

### Revisit when

Lost messages turn out to matter enough that callers need a way to react. The engine-wide error design now exists
([engine/error ADR-005](../../error/adr/ADR-005-logging-ownership.md)). Logger failures stay on the counters in this
ADR: logging does not return `Result` and does not depend on `viscera/error`.
