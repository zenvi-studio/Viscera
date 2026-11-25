# ADR-005: Logging ownership {#adr_err_005}

| Field        | Value                                                                                                                                |
|--------------|--------------------------------------------------------------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                                                                             |
| **Date**     | 2026-11-24                                                                                                                           |
| **Deciders** | Cas Rovers                                                                                                                           |
| **Module**   | `error` (`viscera`)                                                                                                                  |
| **Related**  | Depends on: ADR-004, ADR-006, logging ADR-003, logging ADR-007, logging ADR-008, logging ADR-014, logging ADR-015 · Supersedes: none |

## Context

`Error` will show up in log lines. If every layer that creates, wraps, or forwards an error also logs it, one missing
texture becomes three lines, and a worker that logs from inside a job races the thread that is supposed to decide the
outcome.

[Logging ADR-003](../../logging/adr/ADR-003-no-throw-error-handling.md) already decided that logging calls return
`void` and record their own failures as counters. Its revisit clause named this design. The conclusion of that revisit
is in this ADR: the logger stays on counters. It does not start returning `Result`, and it does not include
`viscera/error`.

[Logging ADR-015](../../logging/adr/ADR-015-crash-handling.md) keeps `Fatal` as a
severity. [ADR-006](ADR-006-panic-assertions-and-exceptions.md)
is the abort path. A controlled process exit is a third thing: the top-level handler logs, flushes, and returns.

Logging categories today are `Core`, `Render`, `Audio`, `Asset`, `Input`, and `Game`
([logging ADR-007](../../logging/adr/ADR-007-levels-and-category-filtering.md)). Future error domains such as `Io` and
`Platform` have no matching category. This ADR does not add categories to make them match.

`game/src/main.cpp` currently initializes the logger, logs `Engine::version()`, shuts the logger down, and returns.
There is no engine init operation yet. `Engine::version()` is a pure query and stays one.

## Decision

**Creating an error does not log. Propagating an error does not log. The layer that handles the error logs it once,
through the existing logger, by formatting the `Error` as a `{}` argument. Logger failures stay on the logger's
counters.**

Details:

1. **The integration point is `fmt::formatter<viscera::Error>` from
   [ADR-004](ADR-004-context-cause-and-origin.md).** A handler writes
   `logging::warning(category, "using placeholder: {}", error)` (or `error`, `fatal`, `debug`). Construction,
   `fail`, `withContext`, `wrap`, and a bare `return std::unexpected(...)` do not call the logger.
2. **A layer is a handler when it decides the outcome.** It recovers (fallback, retry, skip), turns the error into
   something the user sees, discards it on purpose, or ends the process. Forwarding the `Result` is not handling.
3. **One log line is a consequence of point 1, not of a flag on the `Error`.** The chain is immutable and shared.
   An "already logged" bit would be mutable state on that shared value. Do not add one.
4. **The handler chooses level and category.** The same `NotFound` is a warning for a texture that has a placeholder
   and an error for a required player model. Severity is not stored on `Error`
   ([ADR-001](ADR-001-engine-wide-error-model.md)).
   The category is the handler's category. It is not derived from `ErrorDomain`. No domain-to-category table is added.
   When the handler lives in a subsystem that already has a category, use that category. When it does not (`Io`,
   `Platform`, or engine startup), use `Category::Core` until a logging ADR adds a category. This module does not add
   one.
5. **Do not log at the origin, including at `Debug`.** The origin file and line are already on the error
   ([ADR-004](ADR-004-context-cause-and-origin.md)). Origin logging is what produces duplicates. A handler that
   discards an error on purpose logs it at `Debug` with the reason. It does not cast the `Result` to `void` to silence
   `[[nodiscard]]`.
6. **Workers return `Result`.** The consumer on the owning thread logs. Copying an `Error` across threads is safe
   because frames are immutable (ADR-004).
7. **A controlled exit at the top of the process is a handler.** When an engine operation that returns `Result` fails
   and `main` cannot continue:

    1. Log the error once at `logging::Level::Fatal` with the handler's category.
    2. `logging::flush()` within the configured timeout
       ([logging ADR-014](../../logging/adr/ADR-014-logger-lifetime-and-shutdown.md)).
    3. Shut down initialized systems in the order those systems document. The logger is shut down after the systems
       that might still log.
    4. Return a non-zero exit code.

   `Fatal` still does not abort. `panic()` is not used for this class
   ([ADR-006](ADR-006-panic-assertions-and-exceptions.md)). The first such operation, when it exists, is engine
   initialization. `main` owns the decision. `Engine::version()` does not become a `Result`.
8. **The logger's own failures stay counters.** Do not route overflow, argument-budget, formatting failure, or sink
   failure through `Error`. A debug overlay may later read `logging::counters()`. That overlay is out of scope here.
   Logging public headers and `Logger.cpp` do not include `viscera/error`. Error public headers do not include
   `viscera/logging`. The formatter specialization for `Error` lives with `Error`, which is allowed to use `{fmt}`
   ([ADR-001](ADR-001-engine-wide-error-model.md)). The logger formats it through `{fmt}` without naming the error
   module.
9. **Passing an `Error` through the real capture path is required behaviour, specified as tests in
   [ADR-008](ADR-008-testing-and-toolchain-baseline.md).** The formatter must not mutate nodes. The rendered chain must
   fit the logger's output budget or truncate the way ADR-004 already truncates. Creation and propagation still produce
   zero log records. A handled error produces one.

## Alternatives considered

### Log when the error is created

The origin is the best place to see the failure, and a debug log there would lose nothing. It was not chosen because
every higher layer that also logs produces a duplicate, and the origin location is already stored on the value.

### An "already logged" flag

The flag would let a later layer see that somebody logged. It was not chosen because the node is immutable and shared
across threads. A flag is a write. The rule "only handlers log" makes the flag unnecessary.

### Map `ErrorDomain` onto `logging::Category`

One table would pick the category automatically. It was not chosen because the handler's subsystem is who should be
named in the log prefix, and because `Io` and `Platform` have no categories. Automatic mapping would force a logging
change for every new domain.

### Make logging calls return `Result`

Logger failures would become ordinary errors. It was not chosen because logging ADR-003 rejected it: callers cannot
do anything useful, the call sites would be littered with ignored results, and the logger would depend on a module
that wants to log. This ADR is the revisit that clause asked for, and the decision stands.

## Consequences

### Positives

- A recovered `NotFound` produces one warning, from the layer that substituted a placeholder.
- An error that climbs three layers produces one line at the top, with the whole chain in the `{}` text.
- The logger's ADR-003 contract is unchanged. Error handling did not create a dependency cycle.
- `main` has an explicit exit sequence that uses `flush` and shutdown instead of `abort`.

### Negatives

- A programmer who logs "just in case" at the origin violates the rule, and the type system will not catch it. Review
  and the exactly-once tests are the check.
- Discarding an error requires a `Debug` line. That is more work than `(void) result`, and it is required.
- Panic diagnostics and `Fatal` log lines are different streams. Operators read both
  ([ADR-006](ADR-006-panic-assertions-and-exceptions.md)).

### Revisit when

A logging category is genuinely needed for a new subsystem. Add it in a logging ADR. Do not add it here to mirror
`ErrorDomain`. Also revisit if a measured `Error` no longer fits the 256-byte capture slot. That revisit belongs to
[ADR-004](ADR-004-context-cause-and-origin.md) and logging ADR-011 together.
