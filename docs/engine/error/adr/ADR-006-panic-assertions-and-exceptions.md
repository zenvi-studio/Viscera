# ADR-006: Panic, assertions, exceptions, and out-of-memory {#adr_err_006}

| Field        | Value                                                                             |
|--------------|-----------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                          |
| **Date**     | 2026-11-24                                                                        |
| **Deciders** | Cas Rovers                                                                        |
| **Module**   | `error` (`viscera`)                                                               |
| **Related**  | Depends on: ADR-001, ADR-004, logging ADR-014, logging ADR-015 · Supersedes: none |

## Context

`Error` and `Result` report that the world disagreed with the program: a missing file, a lost device, a bad config
value. They are the wrong tool when the program itself is wrong, and they are the wrong tool when the allocator can no
longer build an `Error`.

The logger already draws a line this module must not blur.
[Logging ADR-015](../../logging/adr/ADR-015-crash-handling.md) says `Fatal` does not abort, throw, or flush. A
controlled
exit may log `Fatal`, then `flush`, then shut down
([logging ADR-014](../../logging/adr/ADR-014-logger-lifetime-and-shutdown.md)).
[Logging ADR-003](../../logging/adr/ADR-003-no-throw-error-handling.md) keeps the logger's own failures inside the
logger. The logger also has a private `contractAssert` in `Logger.cpp`. That helper stays private. Routing it through
`panic()` would let an assertion re-enter the logger.

`VISCERA_ENABLE_ASSERTS` exists only as a comment in `engine/CMakeLists.txt`:

```cmake
#target_compile_definitions(VisceraEngine PUBLIC $<$<CONFIG:Debug>:VISCERA_ENABLE_ASSERTS>)
```

The policy has to become real, including for RelWithDebInfo. `VISCERA_LOG_MIN_LEVEL` already treats Debug and
RelWithDebInfo as the inspected configurations, and Release and MinSizeRel as the shipping ones. Asserts follow that
split.

The project builds with exceptions enabled. The logger catches exceptions from formatting, thread startup, and sinks.
`{fmt}` and the standard library can throw. This ADR does not claim the engine is ready for `-fno-exceptions`.

## Decision

**Five failure classes each have one mechanism. Programmer faults and allocation failure call `panic()`, which writes
to stderr and aborts without touching the logger. Recoverable problems return `Result`. Environment failures that the
process cannot survive propagate to `main`.**

Details:

1. **The five classes.**

   | Class                     | Example                                           | Mechanism                           | What happens next                                    |
   |---------------------------|---------------------------------------------------|-------------------------------------|------------------------------------------------------|
   | Normal outcome            | Key missing, queue empty, asset not ready         | `std::optional` or a status enum    | The caller continues                                 |
   | Recoverable failure       | Missing file, bad shader, bad config value        | `Result<T>`, logged by the handler  | Fallback, skip, or a user-facing report              |
   | Unrecoverable environment | Device lost with no fallback, no window           | `Result` propagated to `main`       | One `Fatal` log, `flush`, orderly shutdown, non-zero |
   | Allocation failure        | `std::bad_alloc` where an `Error` cannot be built | `panic()`                           | Immediate abort, no `Error` context                  |
   | Bug or broken invariant   | Impossible state, `*result` on an error           | `VISCERA_ASSERT` or `VISCERA_PANIC` | `panic()`: stderr, then abort                        |

   An environment failure does not abort from deep inside a subsystem. Returning the error lets destructors run and
   lets `main` follow ADR-014. A violated invariant aborts immediately. There is no stack unwinding from `panic()`.

2. **`panic` is independent of logger health.**

    ```cpp
    [[noreturn]] void panic(
        const char* message,
        std::source_location where = std::source_location::current()) noexcept;
    ```

   The body formats a bounded diagnostic into a 1024-byte stack buffer, writes those bytes to the stderr file
   descriptor with a platform write that does not use iostreams and does not allocate (`write` / `_write` on the
   stderr descriptor, or the equivalent), and then calls `std::abort()`. If the formatted text does not fit, it is
   truncated and a fixed end marker occupies the last bytes. If formatting throws, the catch writes a fixed literal
   that includes the file and line and no caller text, then aborts. `panic` does not call `logging::flush`, does not
   call any logging function, does not read a logger "is running" flag, and does not use a replaceable global handler.
   It behaves the same way before `logging::initialize`, after `logging::shutdown`, with no console sink, while the
   logger is failing, and on a thread that must not wait for the backend. `Panic.cpp` does not include a logging
   header.

3. **Assertions compile in on Debug and RelWithDebInfo.** Release and MinSizeRel leave `VISCERA_ENABLE_ASSERTS`
   undefined. The CMake line that replaces the comment above is:

    ```cmake
    target_compile_definitions(VisceraEngine PUBLIC
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:VISCERA_ENABLE_ASSERTS>)
    ```

   `VISCERA_PANIC` is present in every configuration. It calls `panic`.

    ```cpp
    #if defined(VISCERA_ENABLE_ASSERTS)
        #define VISCERA_ASSERT(condition, message)                              \
            (static_cast<bool>(condition)                                       \
                ? static_cast<void>(0)                                          \
                : ::viscera::detail::assertFailed(                              \
                    #condition, (message), ::std::source_location::current()))
    #else
        #define VISCERA_ASSERT(condition, message) \
            static_cast<void>(sizeof(static_cast<bool>(condition)))
    #endif

    #define VISCERA_PANIC(message) \
        ::viscera::panic((message), ::std::source_location::current())
    ```

   When asserts are compiled out, the condition is an unevaluated operand. It must not be relied on for side effects.
   `assertFailed` is `[[noreturn]]` and calls `panic`. External input (files, network data, config, user text) is
   validated with `Result`, never with an assertion. Assertions disappear in Release.

4. **Out of memory is fatal.** `std::bad_alloc` is not converted into an `Error`. Context construction in
   [ADR-004](ADR-004-context-cause-and-origin.md) catches it and calls `panic`. A thread entry point that sees
   `std::bad_alloc` does the same. The panic path itself does not allocate.

5. **Engine code does not throw.** Exceptions stay enabled in the build. They are caught at a foreign-library boundary
   or at a thread entry point, not at every call. A `bad_alloc` at either place follows point 4. Any other exception
   at a subsystem boundary becomes that subsystem's catch-all code, with the exception type name in the message
   ([ADR-007](ADR-007-boundary-translation.md)). There is no `CoreError::Internal` for this. A thread entry point that
   cannot let an exception escape, and that is not a specific subsystem boundary, calls `panic`. A `noexcept` function
   is not, by itself, a reason to terminate: do not mark a function `noexcept` unless its whole body is non-throwing
   or its contract is to abort. `panic`, code-only `Error` construction, and the context constructors (because they
   panic on `bad_alloc` instead of throwing) are the `noexcept` cases from
   [ADR-003](ADR-003-result-and-propagation.md).

6. **Tests use GoogleTest death tests.** They cover abort, the fallback literal, and a panic issued when the logger
   has never been initialized. A panic death test does not call `logging::flush`. There is no test hook that replaces
   `panic`.

A `noexcept` logging call stays under the logging ADRs. This module does not wrap those calls in `Result`.

## Alternatives considered

### Log `Fatal`, flush, then abort inside `panic`

The handler would reuse the logger's direct console path. It was not chosen because `panic` must work when the logger
is not initialized, has no console sink, or is the thread a `flush` would wait on. `flush` from the backend thread or
from a sink waits for itself. `Fatal` remains the controlled-exit severity. It is not the abort path.

### A replaceable panic handler

Tests could install a handler and avoid killing the process. It was not chosen because a process-wide function pointer
is the mutable global this design avoids. Death tests already observe abort.

### Turn exceptions off

`-fno-exceptions` would make "engine code does not throw" a compiler check. It was not chosen because the logger,
`{fmt}`, and GoogleTest are not audited for that flag. Revisit only after every dependency and every boundary has
been checked.

### Treat `std::bad_alloc` as a recoverable `Error`

Callers could free caches and retry. It was not chosen because the context path, the formatter, and many standard
containers cannot report allocation failure without allocating. One policy covers all of them: abort, with a
diagnostic that does not allocate.

### Assert on malformed files

An assertion is a one-line check. It was not chosen because `VISCERA_ASSERT` is absent from Release. A bad file would
pass the check in the configuration that ships.

## Consequences

### Positives

- A programmer fault still prints a diagnostic when logging is down.
- Recoverable failures unwind normally and shut down through the existing logger lifetime.
- Debug and RelWithDebInfo both run the assert expressions. Release does not pay for them.
- The logger's private `contractAssert` cannot recurse through `panic` into the logger.

### Negatives

- Panic output is not a logger record. It will not be in the log file unless stderr is redirected there.
- Death tests are picky about forked processes and extra threads. The panic tests have to stay small.
- A context `bad_alloc` aborts even when the original operation could have returned a code-only `Error`. That is the
  allocation policy, and it will surprise anyone who expected the message to be dropped.
- RelWithDebInfo is slower than a shipping build by the assert checks. That matches the logging floor split. It is
  still a cost.

### Revisit when

A shipping build needs asserts on without turning on the Debug log floor, or a real crash has to be diagnosed from
queued log records. The second case belongs to a new logging ADR, as logging ADR-015 already says. It does not turn
`panic` into a logger client.
