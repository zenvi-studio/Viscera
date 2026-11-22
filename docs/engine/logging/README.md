@page engine_logging_index Engine: Logging

# Logging module

Architectural contract for `viscera::logging`. Implementation belongs under `engine/`; this page collates the public
surface, configuration, and the terms the ADRs share. Design rationale lives in the ADRs.

## Glossary

| Term                  | Meaning                                                                                                                                 |
|-----------------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| **Record**            | The captured log event: header scalars, format-string pointer, format/destroy thunks, owned arguments.                                  |
| **Slot**              | One cell of the ring; holds a record or a control record. Occupied until the destroy thunk has run.                                     |
| **Line**              | The formatted text offered to sinks, matching the pattern below.                                                                        |
| **Floor**             | Compile-time minimum severity (`VISCERA_LOG_MIN_LEVEL`). Convenience functions below it are emptied; `log()` still compares at runtime. |
| **Min level**         | Process-wide runtime threshold. Never below the floor.                                                                                  |
| **Effective minimum** | Per-category override if set, otherwise the min level.                                                                                  |
| **Overflow**          | Producer drop because the ring is full (log or control record).                                                                         |
| **Argument-budget**   | Producer drop because the 256-byte payload does not fit, or a C string has no NUL in the remaining budget.                              |
| **Not-running**       | Producer drop before `initialize` or after `shutdown` has begun.                                                                        |

## Public API

Namespace: `viscera::logging`. Call sites include the logging headers only; they do not `#include <fmt/...>` unless they
define an `fmt::formatter<T>` ([ADR-002](adr/ADR-002-formatting-library-fmt.md),
[ADR-008](adr/ADR-008-custom-type-formatting.md)).

### Logging calls ([ADR-006](adr/ADR-006-macro-vs-function-api.md), [ADR-007](adr/ADR-007-levels-and-category-filtering.md))

All `noexcept`, return `void`. Format parameter is the ADR-005 wrapper (compile-time-checked format string + source
location).

- `trace`, `debug`, `info`, `warning`, `error`, `fatal` — each takes `Category`, then the wrapper, then arguments.
- `log(Level, Category, wrapper, args...)` — generic entry. Runtime-compares against the **floor** before capture.
- Explicit-location helper — for engine wrappers that log on someone else's behalf. `Fatal` on this helper, on `fatal`,
  and on `log(Level::Fatal, ...)` all take the direct console path ([ADR-015](adr/ADR-015-crash-handling.md)).

Argument expressions are always evaluated. Disabled calls return before capture.

### Lifetime and control ([ADR-014](adr/ADR-014-logger-lifetime-and-shutdown.md))

- `initialize(config) -> bool` — once, from the main thread, **before other threads log**.
- `shutdown()` — `noexcept`, idempotent. **After other threads have stopped logging.** Drain, then join, then destroy.
- `flush()` — wait until the ring is empty and sinks flushed, or until the timeout. Timeout **abandons unstarted**
  records; `join` still waits for an in-flight `write`.

Engine sequencing: `initialize` → threads log → those threads stop → `shutdown`.

### Filter and context

- `setMinLevel`, `setCategoryEnabled`, `setCategoryMinLevel`, `clearCategoryMinLevel`
  ([ADR-007](adr/ADR-007-levels-and-category-filtering.md))
- `setFrame` ([ADR-012](adr/ADR-012-timestamps-and-frame-context.md))
- Sink `setEnabled` ([ADR-013](adr/ADR-013-sink-abstraction.md))

### `initialize` config

| Field              | Role                                                                                                                  | ADR                                                |
|--------------------|-----------------------------------------------------------------------------------------------------------------------|----------------------------------------------------|
| Queue capacity     | Power of two; default 8192. Tests pass a small ring.                                                                  | [009](adr/ADR-009-queue-topology.md)               |
| Flush timeout      | Default one second. Bounds unstarted records on `flush`/`shutdown`.                                                   | [014](adr/ADR-014-logger-lifetime-and-shutdown.md) |
| Initial sink list  | `unique_ptr<Sink>` set. Usual engine set: stderr + file. Tests: memory sink, no file.                                 | [013](adr/ADR-013-sink-abstraction.md)             |
| File path          | Opened only if the config asked for a file sink. Append, UTF-8, no rotation in v1. Failed open: sink starts disabled. | [013](adr/ADR-013-sink-abstraction.md)             |
| Source-root prefix | Stripped from file paths on the backend.                                                                              | [005](adr/ADR-005-source-location-capture.md)      |

Level, category, frame, and per-sink enabled flags stay changeable after `initialize`.

## Line pattern

Default, and the pattern tests may rely on ([ADR-013](adr/ADR-013-sink-abstraction.md)):

```
[elapsed][frame][thread][level][category][file:line] message
```

Elapsed time is seconds with millisecond precision from the producer timestamp. Level and category names are lowercase.
The function name is stored on the record and is not printed. The shared line is plain text; `ConsoleSink` adds SGR
color only when stderr is a terminal.

## Counters

Readable as a snapshot; tests do not inspect atomics directly ([ADR-003](adr/ADR-003-no-throw-error-handling.md),
[ADR-016](adr/ADR-016-testing-and-benchmarking.md)).

| Counter                 | What it counts                                                            |
|-------------------------|---------------------------------------------------------------------------|
| Overflow                | Ring full (log records and control records).                              |
| Argument-budget         | Payload over 256 bytes, or unterminated C string in the remaining budget. |
| Not-running             | Log before `initialize` or after `shutdown` has begun.                    |
| Formatting failures     | Backend format thunk threw or buffer growth failed.                       |
| Write failures per sink | `write` returned false or threw.                                          |
| Flush-timeout           | `flush`/`shutdown` hit the timeout and set abandon.                       |

## CMake options

| Option                    | Role                                                                                                                     |
|---------------------------|--------------------------------------------------------------------------------------------------------------------------|
| `VISCERA_LOG_MIN_LEVEL`   | Compile-time floor. Debug/RelWithDebInfo default `Trace`; Release/MinSizeRel default `Info`.                             |
| `VISCERA_LOG_DEBUG_SINKS` | Compile debugger output and the in-engine console sink. Default on for Debug/RelWithDebInfo, off for Release/MinSizeRel. |

`{fmt}` is fetched with a pinned tag via FetchContent when implementation lands (foundation ADR-005,
[ADR-002](adr/ADR-002-formatting-library-fmt.md)).

## Architecture in one picture

Producers filter, construct owned arguments into a slot (format thunk + destroy thunk), and push. One backend thread
waits, formats **from the occupied slot**, destroys and frees, then writes sinks. Queue mutex is never held across
`Sink::write`/`flush`. `Fatal` additionally formats into a 1024-byte stack buffer and `try_lock`s the console, then
unlocks, then enqueues.

```
 game thread   ─┐
 render thread  ├─► filter ─► capture ─► [ ring ] ─► backend: format ─► destroy/free ─► sinks
 worker threads ┘            (no I/O except Fatal)
```

Class and sequence diagrams are deferred until these ADRs are the implementation contract.

## ADRs

- \subpage adr_log_001 — Producers enqueue unformatted records; one backend thread formats them and writes to sinks.
- \subpage adr_log_002 — Use `{fmt}`, pinned via FetchContent, so formatting is the same on every toolchain.
- \subpage adr_log_003 — Logging calls are `noexcept` and return void; failures are counted inside the module.
- \subpage adr_log_004 — Public format strings are checked at compile time; the backend may use a runtime format path.
- \subpage adr_log_005 — Capture the caller with `std::source_location` in a format-string wrapper, stored as static
  pointers.
- \subpage adr_log_006 — The call-site API is functions that forward the format-string wrapper; macros are not required.
- \subpage adr_log_007 — Six ordered levels and a category bitmask, filtered on the producer before any capture.
- \subpage adr_log_008 — Records own constructed arguments; one format thunk and one destroy thunk; bounded C-string
  copy.
- \subpage adr_log_009 — One bounded mutex-protected MPSC ring; format from slot, then destroy/free, then I/O.
- \subpage adr_log_010 — A full queue drops the new record and counts it; producers do not wait for space.
- \subpage adr_log_011 — Slots are preallocated with a 256-byte inline argument buffer, so the hot path does not
  allocate.
- \subpage adr_log_012 — Each record stores steady-clock time, an OS thread id, and a frame number published by the game
  loop.
- \subpage adr_log_013 — `ConsoleSink` colors the whole line by level on a terminal; debug sinks are omitted from
  release.
- \subpage adr_log_014 — One process-wide logger; drain vs abandon; no concurrent log during `shutdown`.
- \subpage adr_log_015 — A hard crash may lose queued records; every `Fatal` entry point also writes directly to the
  console.
- \subpage adr_log_016 — GoogleTest covers logger behaviour; benchmarks are an opt-in target and are not a CI gate.
