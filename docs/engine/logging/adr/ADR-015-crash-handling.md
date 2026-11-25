# ADR-015: Crash handling {#adr_log_015}

| Field        | Value                                                                                                                                                                                       |
|--------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                                                                                                                                    |
| **Date**     | 2026-11-18                                                                                                                                                                                  |
| **Deciders** | Cas Rovers                                                                                                                                                                                  |
| **Module**   | `logging` (`viscera::logging`)                                                                                                                                                              |
| **Related**  | Depends on: ADR-001, ADR-003, ADR-008, ADR-010, ADR-011, ADR-013, ADR-014 · Supersedes: none · See also: [engine/error ADR-006](../../error/adr/ADR-006-panic-assertions-and-exceptions.md) |

## Context

ADR-001 accepts a real limit of the asynchronous design: a record that has been queued and not yet written lives only
in the ring. If the process is killed, segfaults, or calls `abort`, that memory disappears with it. The same ADR says
to revisit the architecture if the engine needs logs to survive hard crashes in a way an async logger cannot provide.
This ADR decides how far the first version goes toward that, and how far it refuses to.

There are two different events people call a crash:

- **A controlled fatal path.** The engine has detected a failure, it is still running on a normal thread, and it wants
  a line on the console before it shows an error or exits. The heap, the mutexes, and the sinks are still in an
  ordinary state.
- **A hard crash.** A signal, a structured exception, `abort`, or an external kill. The crashed thread may hold the
  queue mutex or the allocator lock. Another thread may be inside `{fmt}`. A handler that allocates, locks, or calls
  the normal logger can deadlock or recurse.

ADR-003 forbids the logger from taking the process down. `Fatal` is a severity (ADR-007), not an instruction to abort.
Whether the process exits is the engine's decision, made after the log call returns. How the process terminates is
[engine/error ADR-006](../../error/adr/ADR-006-panic-assertions-and-exceptions.md). This ADR does not change: `Fatal`
remains a severity and does not abort.

ADR-014's `flush` can drain the ring when the process is still healthy. It is the wrong tool inside a signal handler,
and it can block for the flush timeout, so it should not be hidden inside every `Fatal` call.

Constraints:

- **No signal handler in this version.** No `std::signal`, no vectored exception handler, no crash-time walk of the
  ring.
- **The normal producer path still does not format and does not touch sinks** (ADR-001). A fatal line that must be
  visible even when the ring is full needs a narrow exception, written down as an exception, and applied to **every**
  `Fatal` entry point, not only `fatal()`.
- **That exception must not allocate the line, and it must not wait for the ring.**
- **A hard crash may lose the queue.** Pretending otherwise would require the handler this ADR refuses to write.

## Decision

**Queued records may be lost on a hard crash, and the logger installs no crash handler. On every `Fatal` entry point
the line is also written directly to the console sink from the caller, into a stack buffer, then the console mutex is
released, and then the record is queued for the other sinks.**

Details:

1. **There is no crash handler.** A segfault, an abort, or a kill does not run logger code. Records still in the ring,
   and the backend's format buffer, are gone. This is the delivery limit ADR-001 documented.

2. **`Fatal` does not abort, throw, or call `flush`.** It returns like every other logging call. The engine may call
   `flush` afterwards on the controlled path, and it may then exit. `flush` is the ADR-014 wait, with the same timeout.

3. **The direct console path applies to every `Fatal` entry point:** `fatal(...)`, `log(Level::Fatal, ...)`, and the
   explicit-location helper when its level is `Fatal` (ADR-006). `error` and below stay on the queue alone. Widening
   the exception would put formatting and I/O back on the producer for ordinary failures.

4. **The caller writes the line to the console sink** before it tries to enqueue. The line is formatted with `{fmt}`
   into a 1024-byte stack buffer (`fmt::format_to_n` or equivalent). The logger does not allocate that buffer. If the
   formatted text does not fit, it is truncated and a fixed end marker is stored in the last bytes. If formatting
   throws, the catch writes a fixed literal that includes the source location and no user arguments, and the call still
   returns. This 1024-byte **output** limit is independent of the 256-byte **argument-capture** budget in ADR-011. A
   `Fatal` call can print a truncated console line and still be dropped from the ring for not fitting in the slot.

5. **The direct write uses the console sink mutex from ADR-013.** It is a `try_lock`. If the mutex is already held, the
   direct write is skipped. That covers a `Fatal` that runs while the backend is inside the console sink. On success
   the path writes, **unlocks the console mutex**, then enqueues. It does not hold the console mutex while taking the
   queue mutex (ADR-009, ADR-013). The enqueue still happens if `try_lock` failed. Sinks and formatters are still
   forbidden to call the logger (ADR-008, ADR-013). The `try_lock` is the backstop, not a licence to log from a sink.

6. **The record is then captured and enqueued on the normal path**, including the ADR-011 budget check, with a flag the
   header reserves (ADR-011) saying the console already has the line. The backend skips the console sink for that
   record and writes the other sinks, so the line is not doubled on stderr when the queue accepts it. If the direct
   write was skipped, the flag stays clear and the backend writes the console as usual.

7. **If capture or enqueue fails, the console line may already exist.** A full ring drops the enqueue and counts it
   (ADR-010). An oversize argument pack drops the enqueue and counts it (ADR-011). In both cases the stack-buffer line
   is already on the console when `try_lock` succeeded. The file sink does not get a copy. That is the tradeoff: the
   interactive line is favoured, the durable copy still depends on the queue.

8. **The stack-buffer format is the same pattern as ADR-013**, truncated to 1024 bytes. It is a best-effort copy of the
   line the backend would have produced, not a second style. Producing it means the caller samples timestamp, thread
   id, frame, source location, level, and category first (ADR-012), then formats. The buffer stays plain text. The
   direct write is `ConsoleSink::write(Level::Fatal, line)`, so the sink applies the fatal color when stderr is a
   terminal, under the same rule as every other console line.

## Alternatives considered

### A signal handler that flushes the ring

On a crash the handler locks the queue, formats every pending record, and writes the file. This is the version that
would actually keep the last lines. It was not chosen because a handler that takes the queue mutex can deadlock on the
mutex the crashed thread holds, and because formatting allocates and is not async-signal-safe. Doing it wrong is worse
than losing the lines. A raw dump that does not format and does not lock is the revisit, and it wants its own design.

### Accept the loss and do nothing special for `Fatal`

`Fatal` is only a level. The line is queued, and `flush` is the engine's problem. It was not chosen because a fatal
call is often the last thing the engine does before it tears down, and a full ring or a backend that has not been
scheduled yet would lose the one line a person at the console needs. The stack-buffer write is small and does not
change the hard-crash story.

### Direct write only on `fatal()`, not on `log(Level::Fatal, ...)`

Helpers and generic `log` would skip the console path. It was not chosen because the guarantee would then depend on
which function the caller picked, and the explicit-location helper would silently lose it. The exception is the
severity, not the name of the function.

### `Fatal` only writes the console and does not enqueue

The file would miss fatal lines unless the engine flushes a different copy. It was not chosen because the controlled
path is the common one: the process is alive, the ring has room, and the file should contain the same line. The flag
on the record keeps the console from printing it twice.

### Synchronous logging for the whole process once `Fatal` has been called

After the first fatal line, every later log would format and write on the caller. It was not chosen because it turns
one severity into a mode change for every thread, which is a second implementation of the logger hiding inside the
first. The direct write is one call, one sink, one buffer.

## Consequences

### Positives

- The hard-crash behaviour is honest: queued lines can be lost, and the logger does not make a crash worse by running
  unsafe code in a handler.
- On a controlled fatal path the console shows the line even when the ring is full or the backend is behind, without
  allocating and without waiting for a slot, regardless of which `Fatal` function was called.
- When the queue accepts the record, the file and the other sinks still receive it, and stderr does not show a
  duplicate.
- `Fatal` stays `noexcept` and still returns. The engine decides whether the process exits.
- `flush` remains available for the engine to drain the ring before a controlled exit.
- Unlock-before-enqueue keeps console and queue mutexes from being held together.

### Negatives

- A hard crash loses every record the backend has not written. There is no tombstone file and no partial dump.
- The direct `Fatal` path formats on the caller, which is a deliberate break from "producers do not format". It is
  limited to that severity, and it still costs the caller's time for `{fmt}` and a console write when the mutex is
  free.
- The 1024-byte stack line can be truncated when the backend's later line would have been complete. Capture can still
  fail the 256-byte budget after the console has printed. The file copy, if the enqueue succeeded, is the full one.
- If `try_lock` fails and the enqueue is also dropped, the fatal line exists only as a counter increment.
- A formatter that allocates or deadlocks still does so on the `Fatal` caller. The contract in ADR-008 is what keeps
  that off the path. The catch covers a throw. It does not cover a deadlock inside the formatter.

### Revisit when

A real crash has to be diagnosable from the lines that were still queued. The approach to design then is a preallocated
buffer and a handler that only dumps raw slot bytes to an already opened file descriptor, with no lock, no `{fmt}`, and
no allocation. That is a new ADR, not an extension of `Fatal`.
