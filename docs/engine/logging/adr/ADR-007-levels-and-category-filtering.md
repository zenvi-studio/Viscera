# ADR-007: Levels and category filtering

| Field        | Value                                                             |
|--------------|-------------------------------------------------------------------|
| **Status**   | Accepted                                                          |
| **Date**     | 2026-11-18                                                        |
| **Deciders** | Cas Rovers                                                        |
| **Module**   | `logging` (`viscera::logging`)                                    |
| **Related**  | Depends on: ADR-001, ADR-006, ADR-013, ADR-015 · Supersedes: none |

## Context

ADR-001 puts the filter on the producer, before capture, so a message the logger will not keep does not build a record
or touch the queue. This ADR defines what that filter looks at.

Two independent questions get mixed in a lot of loggers:

- **Severity.** How important the message is: a trace of a frame, or a failure the game cannot ignore.
- **Subsystem.** Which part of the engine produced it. While debugging the renderer, the audio log is noise, even when
  every line is the same severity.

The engine does not have those subsystems as code yet. The filter still has to exist before the first call site, or
every later system invents its own. The set of categories should be cheap to test and cheap to extend by one entry.

Constraints:

- **The check is on the hot path.** It is a load and a compare, not a lock, a map lookup, or a string compare against a
  channel name.
- **A disabled message does no capture work** (ADR-001). Argument expressions are still evaluated, because the API is
  functions (ADR-006).
- **Release builds should be able to drop the noisiest severities from the binary** without a second logging API.
- **The logging module stays self-contained.** Categories are an enum in the logger, not a type imported from each
  subsystem. A new subsystem adds an enumerator here when it starts logging.

## Decision

**A message is kept only when its severity passes an effective minimum level and its single category bit is enabled.
The producer reads both through atomics and returns before capture when the message is disabled. The compile-time floor
empties convenience functions below that floor and is also a runtime compare inside `log()`. Release builds compile
with a floor of `Info`.**

Details:

1. **Severities are ordered, lowest to highest:** `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`. They fit in a
   `std::uint8_t`. A message passes the level test when its severity is greater than or equal to the effective minimum.
   `Fatal` is a severity. It does not abort the process and it does not throw (ADR-003, ADR-015). Each level also has
   one fixed console color. That binding is presentation, defined in ADR-013, and it plays no part in this filter.
2. **A category is one bit in a `std::uint64_t`.** The named bits are `Core`, `Render`, `Audio`, `Asset`, `Input`, and
   `Game`. Each message carries exactly one of them. The remaining bits are reserved and are left disabled. Adding a
   subsystem is a new enumerator, not a string.

   ```cpp
   enum class Level : std::uint8_t { Trace, Debug, Info, Warning, Error, Fatal };

   enum class Category : std::uint64_t {
       Core   = 1ull << 0,
       Render = 1ull << 1,
       Audio  = 1ull << 2,
       Asset  = 1ull << 3,
       Input  = 1ull << 4,
       Game   = 1ull << 5,
   };
   ```

3. **Terms.** The **floor** is the compile-time minimum (`VISCERA_LOG_MIN_LEVEL`). The **min level** is the process-wide
   runtime threshold. The **effective minimum** for a message is that category's override if one is set, otherwise the
   min level. The effective minimum is never below the floor.
4. **The effective minimum comes from a process-wide level, which a category may override.** By default there is **no
   override**: every category follows the global min level. `setCategoryMinLevel` replaces the global minimum for that
   category only. `clearCategoryMinLevel` removes the override so the category follows the global min level again,
   including later global changes. The category bit must still be set in the enabled mask. Both the mask and the levels
   are `std::atomic`, read with relaxed ordering on the producer. The filter takes no lock.
5. **Defaults.** Debug and RelWithDebInfo compile every severity (floor `Trace`), and the runtime minimum starts at
   `Trace`, with every named category enabled. Release and MinSizeRel compile with a floor of `Info`, and the runtime
   minimum starts at `Info`. A CMake cache variable, `VISCERA_LOG_MIN_LEVEL`, overrides the floor. The runtime minimum
   can be raised above the floor and lowered back down to it. It cannot show a severity the floor compiled out.
6. **The floor is applied in two places.** Convenience functions below the floor have an empty `if constexpr` body,
   which
   removes capture and enqueue from those functions. Generic `log()` always does a runtime compare against the floor
   and returns before capture when the level is below it, so `log(Level::Debug, ...)` cannot capture when the floor is
   `Info` (ADR-006). Neither form skips evaluation of the call's argument expressions.
7. **Check order inside a compiled-in call:** compare the level with the floor (for `log()`), then with the effective
   minimum, then test the category bit. Any failure returns. Only then does the producer capture and enqueue. `Fatal`
   still takes the direct console path first (ADR-015) before this enqueue filter; a `Fatal` that is then dropped by
   overflow or budget may already have printed.
8. **Configuration functions are `noexcept` and return `void`.** `setMinLevel`, `setCategoryEnabled`,
   `setCategoryMinLevel`, and `clearCategoryMinLevel` adjust the atomics. They are safe to call while other threads log.
   A change applies to later calls; a record already queued keeps the level and category it was captured with.

## Alternatives considered

### Level only, no categories

One minimum severity for the whole process. This is simpler and is what many small engines ship. It was not chosen
because the index of this module already treats subsystem filtering as part of the decision, and a single threshold
cannot quiet one subsystem without quieting the others.

### String or hierarchical channel names

Channels such as `"render.frame"` looked up in a map, in the style of larger logging libraries. This is extensible
without editing an enum. It was not chosen because a string lookup on the hot path contradicts the cheap filter, and
because a hierarchy is more machinery than a handful of engine subsystems needs.

### A thread-local current category

A scope guard sets the category, and the log call does not take one. Call sites are shorter. It was not chosen because
the category is then invisible at the call, worker threads inherit the wrong one, and the filter depends on hidden
state. An explicit argument costs a few characters and is always the category the programmer meant.

### Several categories on one message

The call takes a mask, and the message passes if any selected bit is enabled. It was not chosen because call sites do
not naturally belong to two subsystems, and the override level becomes ambiguous. One bit per message keeps the test a
single compare.

## Consequences

### Positives

- A rejected message does not capture arguments, allocate, or touch the queue.
- The hot-path test is a pair of relaxed atomic loads and compares, with no lock and no heap.
- Release binaries can drop `Trace` and `Debug` without a second API and without losing `Info` and above.
- Per-category overrides let a developer raise or lower one subsystem while the rest stay on the global minimum.
- Categories are a closed enum, so a typo is a compile error rather than a silent new channel.

### Negatives

- Every call site passes a category, including the ones that are obviously `Core`.
- The enum is edited when a new subsystem starts logging. That edit is deliberate, and it is still a change to a shared
  header.
- Sixty-four bits is a hard ceiling. The engine is not close to it.
- The compile-time floor cannot stop argument evaluation, so a debug call that does real work in its arguments still
  does that work in a release build.
- Relaxed atomics mean another thread can log a few messages on the old threshold after a change. That is acceptable
  for a filter. The level is not a synchronisation mechanism.

### Revisit when

A subsystem needs more than one bit, the category list approaches 64, or disabled-call argument cost forces the macro
layer described in ADR-006.
