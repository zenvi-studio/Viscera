# ADR-001: Engine-wide error model {#adr_err_001}

| Field        | Value                                                                                                      |
|--------------|------------------------------------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                                                   |
| **Date**     | 2026-11-24                                                                                                 |
| **Deciders** | Cas Rovers                                                                                                 |
| **Module**   | `error` (`viscera`)                                                                                        |
| **Related**  | Depends on: foundation ADR-001, foundation ADR-002, foundation ADR-004, logging ADR-003 · Supersedes: none |

## Context

The engine has a logger and `Engine::version()`. It has no shared way to report a recoverable failure. Each future
subsystem would otherwise invent its own enum, its own `std::expected` error type, or start throwing. Callers that
cross `io`, `asset`, and `render` would convert at every boundary, and the logger would have nothing generic to format.

The logger already fixes constraints this design has to live with:

- Logging calls are `noexcept`, return `void`, and the logging module does not depend on general error handling
  ([logging ADR-003](../../logging/adr/ADR-003-no-throw-error-handling.md)). The dependency runs one way.
- Logged arguments are copied into a 256-byte inline slot. The copy is `noexcept`. A non-trivial destructor is allowed.
  A formatter must not throw ([logging ADR-008](../../logging/adr/ADR-008-custom-type-formatting.md),
  [logging ADR-011](../../logging/adr/ADR-011-allocation-policy.md)).
- `Fatal` is a severity. It does not abort or flush ([logging ADR-015](../../logging/adr/ADR-015-crash-handling.md)).
- The game may depend on the engine. The engine does not depend on the game
  ([foundation ADR-004](../../../foundation/adr/ADR-004-engine-vs-game-split.md)).
- The language is C++23, extensions are off, and Clang, GCC, and MSVC are all supported
  ([foundation ADR-001](../../../foundation/adr/ADR-001-use-cpp23.md),
  [foundation ADR-002](../../../foundation/adr/ADR-002-clang-gcc-msvc.md)).

A failure that is a normal, frequent outcome (a cache miss, an empty queue, "not ready yet") is not an error in this
module. Those stay `std::optional` or a status enum. [ADR-006](ADR-006-panic-assertions-and-exceptions.md) is the full
split between outcome classes.

## Decision

**Recoverable failures are values of one `viscera::Error` type. The type carries a stable code, a static description,
the source location where it was created, and an optional immutable chain of context frames.**

Details:

1. **One error type crosses every subsystem boundary.** Propagation does not convert between error types. Subsystem
   enums exist, and they erase into this single type. How codes are owned
   is [ADR-002](ADR-002-domains-codes-and-descriptions.md).
2. **`Error` holds four kinds of fact.** The code identifies the failure. The static description is the code's fixed
   meaning (`"asset not found"`). Each frame stores the `std::source_location` of the public call that created it.
   The root origin is the innermost of those locations. The optional chain says what higher-level operation was in
   progress and which earlier error caused this one.
   [ADR-004](ADR-004-context-cause-and-origin.md) defines the chain.
3. **`Error` does not carry a severity, a stack trace, user-facing text, or a `std::any` payload.** The handler chooses
   the log level ([ADR-005](ADR-005-logging-ownership.md)). `std::stacktrace` is not the diagnostic mechanism. Text
   shown to a player is the handler's job. Extra payload belongs in the context message or in a more specific code.
4. **The value is a small, copyable, nothrow handle.** A code-only error does not allocate. Copying an error that has
   context is a reference-count bump. The handle has to fit in the logger's 256-byte argument slot. Size and formatter
   rules are [ADR-004](ADR-004-context-cause-and-origin.md).
5. **Callers return `Result<T>`.** That alias, `[[nodiscard]]`, and propagation are
   [ADR-003](ADR-003-result-and-propagation.md). This ADR only fixes that the unexpected alternative is `Error`.
6. **Each layer does one of four jobs.** It translates a foreign error into an engine code, translates that code into
   its own abstraction, annotates the error with what it was doing, or handles it. Plain propagation leaves the error
   unchanged. Translation rules are [ADR-007](ADR-007-boundary-translation.md). Handling rules are
   [ADR-005](ADR-005-logging-ownership.md).
7. **The module sits under every subsystem and above the standard library and `{fmt}`.** Public headers live in
   `engine/include/viscera/error/`. One umbrella, `<viscera/error/error.hpp>`, exposes the types used in signatures.
   `Error`, `Result`, `ErrorCode`, and `fail` live in `namespace viscera`. Subsystem enums live in the subsystem
   namespace (`viscera::asset`). Macros use the `VISCERA_` prefix.
8. **Logging does not include this module, and this module's headers do not include logging.** `panic` reaches stderr
   from its `.cpp` and does not go through the logger ([ADR-006](ADR-006-panic-assertions-and-exceptions.md)).
9. **The code stays in the existing `VisceraEngine` target.** A separate CMake target for `error` waits until the
   engine is no longer one static library
   ([foundation ADR-003](../../../foundation/adr/ADR-003-cmake-project-layout.md)).
10. **Engine headers do not include game headers.** Game code may define its own error enum later, through the same
    hooks as an engine subsystem. That does not put a `Game` enumerator in the v1 domain list
    ([ADR-002](ADR-002-domains-codes-and-descriptions.md)).

There is no engine-wide error API in the tree today. This ADR is the start of one, not a migration.

## Alternatives considered

### A per-module error type (`std::expected<Texture, TextureError>`)

Each layer names its own `E`. Switching on a texture error is straightforward, and the representation is a bare enum.
It was not chosen because every boundary needs a conversion function, the type has nowhere to put a message or a cause,
and the logger cannot format "an error" without a formatter per `E`.

### `std::error_code`

It is portable, interoperates with `<system_error>` and `<filesystem>`, and supports
`error_condition` equivalence. It was not chosen because categories are virtual singletons, `message()` returns a
`std::string`, and there is no place for an immutable context chain. It becomes the right revisit if third-party or
mod-defined domains have to plug in without editing the engine.

### A polymorphic error (`unique_ptr<IError>`, `exception_ptr`)

A heap error can carry any payload. It was not chosen because every failure allocates, the type is move-only, and the
logger's capture path needs a nothrow copy into the inline slot.

## Consequences

### Positives

- One signature shape covers every subsystem: `Result<T>`, with `Error` as the failure.
- Context can be added later without changing the code callers switch on.
- The logger can format any failure with a single `fmt::formatter<viscera::Error>`.
- Logging stays independent, so a failing diagnostic cannot recurse into error handling.

### Negatives

- The engine owns the `Error` implementation, including the context chain in ADR-004.
- Domains are a closed list. A plugin that wants its own domain has to wait for the revisit below.
- Call sites that only needed a bare enum now carry a handle. Code-only errors stay allocation-free so the common case
  stays cheap; context is the cold path.

### Revisit when

Plugins, user extensions, or separately versioned modules need to introduce error domains without changing engine
headers. The candidate replacement is an open category mechanism. `std::error_code` is the comparison point, not the
default.
