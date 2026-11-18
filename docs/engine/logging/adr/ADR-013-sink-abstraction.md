# ADR-013: Sink abstraction

| Field        | Value                                                             |
|--------------|-------------------------------------------------------------------|
| **Status**   | Accepted                                                          |
| **Date**     | 2026-11-18                                                        |
| **Deciders** | Cas Rovers                                                        |
| **Module**   | `logging` (`viscera::logging`)                                    |
| **Related**  | Depends on: ADR-001, ADR-003, ADR-009, ADR-015 · Supersedes: none |

## Context

ADR-001 ends at the sinks: the backend formats a record once and writes it to the configured destinations. Console,
file, an in-game console, and a network target were the examples. This ADR defines the type those destinations share,
who is allowed to call it, and which of them exist in a release build.

A sink that is callable from any thread forces every sink to take its own lock, and it races the backend. A sink that
formats the line itself duplicates ADR-002 and drifts between destinations. A sink that cannot be turned off forces
debug output into a shipping build. The note this ADR started from is that some sinks can be disabled in release. That
has to be a property of the abstraction, not a special case in `main`.

Failures are already constrained. ADR-003 isolates a failing sink from the others, counts the failure, and leaves the
retry or disable policy to this ADR.

Constraints:

- **The backend thread is the only thread that formats and the only thread that walks the sink list** during normal
  logging. The one exception is the direct console write in ADR-015.
- **Sinks do not need to be thread-safe against producers.** They do need a clear rule about which thread destroys them.
- **One formatted line is shared.** Destinations differ by where the bytes go, not by a second formatter.
- **Adding a destination does not change call sites** (ADR-001).
- **Release builds still log.** Disabling a debug console must not remove `Warning`, `Error`, and `Fatal` from the file
  and from stderr.
- **A sink must not call back into the logger.** Formatting and writing run while a record is in hand. Re-entrant
  logging deadlocks or recurses.

## Decision

**A sink is a logger-owned object with `write` and `flush`, called from the backend thread with one already formatted
plain line and its level. `ConsoleSink` colors the whole line by a fixed color per level when stderr is a terminal.
Console and file sinks exist in every build. Debugger output and the in-engine console are omitted from release
builds. Any compiled-in sink can be turned off at runtime.**

Details:

1. **The interface is:**

   ```cpp
   class Sink {
   public:
       virtual ~Sink() = default;

       virtual bool write(Level level, std::string_view line) = 0;
       virtual void flush() = 0;
   };
   ```

   `write` returns `false` on failure. It may also throw. The backend treats both as a failure of that sink. `flush`
   pushes buffered bytes out. It does not throw out of the backend; the backend catches it if it does. A sink does not
   format, and it does not add the level name or the source location to the text. Those are already in the line. The
   level argument is there so a sink can present the line, including coloring it, without parsing the text.

2. **The logger owns sinks with `std::unique_ptr<Sink>`.** The engine passes the initial set in the `initialize`
   config. The usual engine set is stderr plus a file. Tests pass a memory sink and no file (ADR-016). The logger does
   not open a file the config did not ask for.
3. **The backend writes a record once, then offers that text to each enabled sink.** The default pattern, which is the
   pattern tests may rely on, is one line:

   `[elapsed][frame][thread][level][category][file:line] message`

   Elapsed time is seconds with millisecond precision from the ADR-012 timestamp. The frame, thread id, lowercase level
   name, and lowercase category name come from the record. The file path is the shortened path from ADR-005. The
   function name is stored on the record and is not printed. A sink that wants a different pattern is a later decision.
   The line is a single view into the backend's reusable buffer (ADR-011). Sinks that need to keep it copy it during
   `write`. The buffer contains no color codes.

4. **Callers of `write` and `flush` are the backend thread**, except the console sink's `write`, which ADR-015 may call
   from any `Fatal` entry point. That path serializes with the backend on a mutex that belongs to the console sink, not
   to the queue. **The queue mutex is never held across `Sink::write` or `Sink::flush`** (ADR-009). The fatal path
   `try_lock`s the console mutex, writes, **unlocks**, then enqueues; it does not hold the console mutex while taking
   the queue mutex. No other sink takes a logger lock, and no sink is required to be thread-safe.
5. **Registration after startup is a control record on the ring** (ADR-009). The backend applies it between log
   records: it takes ownership of the new sink or drops an existing one. If the ring is full, the control record is
   dropped and the overflow counter counts it, like a log line. The expected path is to register sinks in the
   `initialize` config, before the backend starts, where the ring cannot be full. Removal destroys the sink on the
   backend thread. `shutdown` destroys whatever remains after the backend has joined, on the thread that called
   `shutdown`.
6. **A sink destructor must not call the logger.** It may run on the backend thread.
7. **Each registration has an atomic enabled flag.** The backend checks it before `write` and before `flush`. A
   disabled sink stays registered and is skipped. `setEnabled` is safe to call from any thread.
8. **Repeated failure disables the sink.** Each `false` return or caught exception increments that sink's failure
   counter (ADR-003). Three consecutive failures disable it. The backend writes one diagnostic to the sinks that are
   still enabled, naming the sink and the count, and then continues. A later success resets the consecutive count. A
   disabled sink is not retried until something calls `setEnabled(true)`. There is no retry loop inside `write`.
9. **A file that cannot be opened at `initialize` does not fail the logger.** That sink starts disabled, its counter
   records the open failure, and the other sinks run.
10. **Release builds omit debug-only sinks.** Console (`stderr`) and file are compiled in every configuration.
    Debugger output (`OutputDebugString` on Windows, the platform equivalent elsewhere) and the in-engine console are
    compiled only when `VISCERA_LOG_DEBUG_SINKS` is on. That option defaults on for Debug and RelWithDebInfo, and off
    for Release and MinSizeRel. It can be overridden from CMake. Those sinks are not linked into a build that has the
    option off, so a release binary does not contain them and cannot turn them on. The in-engine console may be
    implemented later. The flag and the omission rule exist now, so it does not arrive as a release sink by default.
    Console and file remain available in release and can still be disabled at runtime.
11. **Level color is a sink concern, and only `ConsoleSink` applies it.** The shared line stays plain, so a file, a
    memory sink, and a redirected stderr do not contain escape codes. `ConsoleSink` wraps the whole line in one fixed
    color for that level, then resets. The binding is not configurable:

    - `Trace`: bright black (SGR 90)
    - `Debug`: cyan (SGR 36)
    - `Info`: green (SGR 32)
    - `Warning`: yellow (SGR 33)
    - `Error`: red (SGR 31)
    - `Fatal`: bright red (SGR 91)

    The bytes on the stream are the SGR prefix, the unchanged line, and the reset (`\033[0m`). Those three parts are
    written while the console mutex is held, so a direct `Fatal` write (ADR-015) cannot tear a colored line apart. The
    sink does not build a `std::string` to do this. The console mutex is released before the fatal path takes the queue
    mutex.
12. **Color is applied only when stderr is a terminal.** A pipe or a redirect gets the plain line. On Windows the sink
    enables virtual terminal processing for that console so the SGR codes are honored. If enabling it fails, the sink
    writes the plain line. The level name is still in the text, so a monochrome reader can still see the severity.
13. **Other sinks ignore the map.** The file sink, the memory sink, and debugger output write the plain line. An
    in-engine console, when it exists, may use the same level-to-color binding with its own drawing. It receives the
    plain line and the `Level`. It does not receive a string that already contains ANSI codes.
14. **Logger diagnostics are passed as `Warning`.** The overflow summary (ADR-010) and the sink-failure line from point
    8 are given to `write` as `Warning`, so the console shows them in yellow. That level is for presentation. Both
    lines still bypass the filter, as ADR-010 requires for the overflow summary.

## Alternatives considered

### Every sink is thread-safe and producers write it directly

There is no backend walk, and a sink can be called from anywhere. It was not chosen because it undoes ADR-001: every
call site pays for I/O, and every sink grows a lock. The backend exists so sinks can be simple.

### Each sink formats the record itself

The queue would carry the raw record to the sink, and a file sink and a network sink could present it differently. It
was not chosen because formatting is the expensive part ADR-001 put on the backend exactly once. A structured sink can
be added later by giving it the record as well as the line. The first sinks do not need that.

### Sink changes only while the logger is stopped

No control record, no full-queue failure for registration. It was not chosen as the only mechanism because an in-game
console has to attach after startup. The config passed to `initialize` is still the path the engine and the tests use.
The control record is the extension, with the overflow behaviour stated rather than hidden.

### Color the line in the backend buffer

The formatter would wrap the shared text in SGR codes, and every sink would write the same bytes. It was not chosen
because the file, the memory sink, and any redirected console would then store escape codes. Tests that match the plain
pattern would depend on color, and a sink that draws its own colors would have to strip codes it did not ask for.

### Parse the level out of the formatted line

`write` would stay `string_view` only, and `ConsoleSink` would find `[info]` in the text. It was not chosen because the
pattern and the color would be coupled: a change to the brackets would silently color nothing, or color the wrong
span. The level is already known when the backend calls the sink.

### A configurable palette

Each sink, or a config file, would choose colors per level. It was not chosen because a level's color is a fixed
signal: yellow is a warning in every colored sink. A theme can be added later without changing the call or the plain
text.

### Compile every sink into every build and rely on the runtime flag

Release would contain the debugger sink, turned off. It was not chosen because "omitted from release" is stronger than
"default off": the code is not in the binary, and a config mistake cannot turn a debug console on in shipping. The
runtime flag still exists for the sinks that are compiled in.

## Consequences

### Positives

- A new destination is a new `Sink`. Call sites and the queue do not change.
- Sinks other than the fatal console path are single-threaded, so their implementations can do plain file and stream
  I/O.
- One formatted line means every destination shows the same text, which is what tests assert against. Color is added
  only by `ConsoleSink`, and only on a terminal, so the text itself stays comparable.
- Each level has one console color for the whole line, so severity is visible without reading the level name.
- A broken sink disables itself after three failures and leaves the others running.
- Release builds keep console and file, and they do not contain the debugger or in-engine console sinks.
- Any compiled-in sink can still be switched off at runtime without restarting the logger.

### Negatives

- A sink that needs the raw record (a network packet, a structured field) cannot get it from `string_view` alone. It
  would have to parse the line, or wait for the interface to grow.
- The line view is only valid during `write`. A sink that stores the pointer past the return is wrong.
- A control record can be dropped when the ring is full, so a late `addSink` can silently do nothing except increment
  the overflow counter. Registration in `initialize` does not have that hole.
- Sink destructors on the backend thread, and the ban on logging from a sink, are contracts the compiler will not
  check.
- The default pattern is fixed in the logger. One sink cannot print a shorter line without a later change.
- A redirected or non-VT console is uncolored. The level name in the text is the fallback, and it is easy to miss if
  someone expected color in a log file.
- The prefix, the line, and the reset are three writes. They stay atomic only because they share the console mutex. A
  sink that colors without that mutex will tear lines. The fatal path must drop that mutex before enqueue, or it would
  invert lock order with a backend that ever held the queue mutex across `write` (forbidden by ADR-009).
- Debug-sink selection is a compile-time option. Turning the in-engine console on in a Release build means
  reconfiguring and rebuilding, which is the point of the option and is still a cost.

### Revisit when

A sink needs a different pattern or the raw record, late registration has to succeed while the ring is full, or the
console colors need to be configurable. The registration fix is a side channel that does not share the log ring, not a
lock inside every sink. A palette change does not move color into the shared format buffer.
