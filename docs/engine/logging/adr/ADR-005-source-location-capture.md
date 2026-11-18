# ADR-005: Source location capture

| Field        | Value                                                    |
|--------------|----------------------------------------------------------|
| **Status**   | Accepted                                                 |
| **Date**     | 2026-11-18                                               |
| **Deciders** | Cas Rovers                                               |
| **Module**   | `logging` (`viscera::logging`)                           |
| **Related**  | Depends on: ADR-001, ADR-004, ADR-006 · Supersedes: none |

## Context

A log line is much more useful when it says where it came from: the file, the line and the function that called the
logger. Writing that by hand at every call site is tedious and goes stale as code moves, so the logger should capture it
automatically.

Several constraints shape how:

- **The location must be the caller's.** Convenience functions such as `trace()` and `debug()` forward to `log()`
  (ADR-004). A naive capture inside `log()` would report the location of that forwarding code for every message, which
  is useless.
- **The capture must be cheap.** Logging runs on hot paths, and the producer does only the minimum (ADR-001). Resolving
  a location at runtime, for example by walking the stack, is far too expensive.
- **The API is a variadic template with a compile-time-checked format string** (ADR-004). The usual way of capturing a
  call site in C++ is a defaulted `std::source_location` parameter, but a defaulted parameter placed after a parameter
  pack cannot be deduced. `log(Level, fmt, args..., std::source_location loc = std::source_location::current())` does
  not work.
- **Where the data lives matters.** The record is built on the producer thread and read later on the backend thread
  (ADR-001), so whatever is captured must be safe to read after the call has returned, and must not force allocation on
  the hot path (ADR-011).
- **File and function names end up in the binary and in the output.** Full build-machine paths are long, differ between
  machines and developers, and can reveal directory structure in a shipped build.
- **The decision between a macro and a function API is made separately** (ADR-006). The capture mechanism should not
  force that choice.

## Decision

**The logger captures the call site automatically with `std::source_location`, taken at the call site through a wrapper
type that carries the format string and the location together, and stores the result in the log record as pointers to
static data.**

Details:

1. **Capture uses `std::source_location::current()`**, not `__FILE__`/`__LINE__` macros. It provides file name, function
   name, line and column without any macro.
2. **A wrapper type solves the parameter-pack problem.** The first parameter of `log()` and of every convenience
   function is a small type that holds both the compile-time-checked `{fmt}` format string (ADR-004) and the source
   location. It has an implicitly converting, `consteval` constructor that takes the format string and defaults its
   location argument to `std::source_location::current()`. Since the conversion from the string literal happens at the
   caller's call expression, the captured location is the caller's. The argument types are deduced only from the
   trailing arguments, not from the wrapper. Illustratively:

   ```c++
   template <typename... Args>
   struct FormatString {
       fmt::format_string<Args...> format;
       std::source_location location;

       template <typename S>
       consteval FormatString(const S& s,
                              std::source_location loc = std::source_location::current())
           : format(s), location(loc) {}
   };
   ```

   The public `log` / convenience signatures, including `Category`, are the ones in ADR-006. This sketch is the wrapper
   only. The exact form is an implementation matter, and tests (ADR-016) must verify that the reported line is the
   caller's for `log()` and for every convenience function. The `consteval` constructor plus defaulted
   `source_location` must be verified on Clang, GCC, and MSVC (foundation ADR-002).
3. **This refines ADR-004.** The compile-time-checked format string and the location travel in one parameter, and the
   rule from ADR-004 still applies: convenience functions take this wrapper directly and forward it, never a plain
   string, otherwise the location (and the check) would be lost.
4. **The location is stored as pointers, not copies.** The file name and function name returned by
   `std::source_location` point into static storage, so the record keeps the pointers and the line number, and nothing
   is allocated or copied on the producer side. This follows the same static-lifetime reasoning as format strings in
   ADR-004. The column is not stored by default, since it adds little value to a log line.
5. **Formatting the location is the backend's job.** The producer always captures it. Whether it is shown, and in what
   form, is decided when the record is formatted and by the sink configuration (ADR-013).
6. **Paths are shortened on the backend.** A configured source root prefix is stripped from the file name when the
   record is formatted, off the hot path. Build-level path mapping options of the toolchain may additionally be used to
   shorten or anonymize the embedded paths, but this ADR does not depend on them.
7. **A lower-level entry point accepts an explicit location.** Helper wrappers in the engine that call the logger on
   someone else's behalf pass the location through that entry point, so the message points at the real call site, not
   at the helper. ADR-006 chose a function API; a later macro layer, if any, would use this same entry point. When the
   level is `Fatal`, this helper takes the direct console path (ADR-015).
8. **Capture is on in all build configurations by default.** Because the data is static, it costs nothing measurable at
   runtime. A build option may later drop it to reduce the size of the binary or avoid shipping source paths; see
   Revisit.

## Alternatives considered

### Macros that capture `__FILE__`, `__LINE__` and the function name

The logging call sites are macros that pass the location along. This works with any compiler and sidesteps the
parameter-pack problem. It was not chosen as the capture mechanism because it forces a macro API on the whole module,
which is the call-site question ADR-006 decided separately (functions, no shipped macros). A macro layer can still be
added on top using the explicit-location entry point in point 7.

### A defaulted `std::source_location` parameter after the argument pack

This is the usual idiom and would need no wrapper type. It does not work with a variadic template, because a parameter
that follows a pack is not deduced and the argument types could not be determined.

### The caller passes the location explicitly

Each call site supplies `std::source_location::current()` itself. This is simple and has no hidden mechanism. It was not
chosen because it burdens every call site with noise, and a missing or wrong argument would silently produce wrong
locations.

### Resolve the location at runtime (stack trace or backtrace)

The logger walks the stack and looks up file and line from debug information. This needs no source changes at all. It
was not chosen because it is orders of magnitude too expensive for a hot path, depends on debug information being
present, and is not portable across the supported toolchains and platforms.

### No source location

Log lines carry only level, category, time and message. This is the simplest and smallest option. It was not chosen
because the location is one of the most useful pieces of information when reading a log, and capturing it costs nothing
at runtime.

## Consequences

### Positives

- Every log line can show the file, line and function that produced it, with no work from the caller.
- The capture is a compile-time constant stored as pointers, so it adds no allocation and no measurable cost on the hot
  path.
- The wrapper type removes the parameter-pack problem without a macro API. ADR-006 chose functions and kept this
  wrapper.
- The compile-time format check and the location come from one parameter, so they cannot drift apart.
- Backend-side path trimming keeps output readable and keeps work off the producer thread.

### Negatives

- The wrapper type and its `consteval` constructor are subtle, and the first parameter of the API is no longer a plain
  format string, which can make error messages and signatures harder to read.
- Every function that forwards to the logger must take the wrapper directly, and one that does not reports its own
  location instead of the caller's. This needs a rule and tests.
- File names and function names are embedded in the binary. Function names from `std::source_location` are
  implementation-defined in format and can be long for templates, and paths can reveal the build environment.
- The record grows by a few pointer-sized fields, which matters for the queue's slot size (ADR-009, ADR-011).
- Helper code that logs on behalf of others must remember to use the explicit-location entry point.

### Revisit when

Binary size or shipping source paths in release builds becomes a concern. A later macro layer (ADR-006 revisit) would
call the explicit-location entry point and would not remove this wrapper from the function API.
