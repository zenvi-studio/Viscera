@page engine_error_index Engine: Error handling

# Error handling

Architectural contract for recoverable failures (`viscera::Error`, `viscera::Result`) and for the separate abort path
(`panic`, assertions). Implementation belongs under `engine/`; this page collates the public surface, the rendering
grammar, and the terms the ADRs share. Design rationale lives in the ADRs.

This folder is the contract. If an earlier design note outside the repository disagrees with an ADR here, the ADR is
the one to implement.

## Glossary

| Term                   | Meaning                                                                                                                     |
|------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| **Error**              | The single failure value: code, static description, frame location, and an optional immutable cause chain.                  |
| **ErrorCode**          | `(ErrorDomain, uint16_t value)`. What callers compare. No `Ok` value.                                                       |
| **ErrorDomain**        | Closed list of code owners. v1 enumerator: `Core`.                                                                          |
| **ErrorEnum**          | Concept for a subsystem `enum class` that provides `errorDomain` and `errorMessage`.                                        |
| **Static description** | Fixed phrase from `errorMessage` (`"file not found"`), captured onto the `Error`.                                           |
| **Origin**             | Frame location of the innermost `Error`. The rendered line prints it when the chain is within the frame cap.                |
| **Frame location**     | `std::source_location` of this `Error` value: the `fail`, `Error`, `withContext`, or `wrap` call.                           |
| **Context**            | The message on a frame: what this layer was doing.                                                                          |
| **Cause**              | The previous `Error`, linked from a `withContext` or `wrap` frame.                                                          |
| **Result**             | Alias of `std::expected<T, Error>`.                                                                                         |
| **Handler**            | The layer that recovers, reports to the user, discards on purpose, or ends the process. The only layer that logs the error. |
| **Translation**        | Turning a foreign code or exception into an engine `ErrorEnum` inside the `.cpp` that called the foreign API.               |
| **panic**              | Stderr diagnostic, then `abort`. Used for broken invariants and allocation failure. Does not use the logger.                |

## Dependency rules

Normative detail: [ADR-001](adr/ADR-001-engine-wide-error-model.md),
[ADR-005](adr/ADR-005-logging-ownership.md).

- Subsystems and the game include `<viscera/error/error.hpp>`. They depend on the engine. The engine does not include
  game headers ([foundation ADR-004](../../foundation/adr/ADR-004-engine-vs-game-split.md)).
- Error public headers depend on the standard library, `{fmt}`, and `viscera::FormatString`. They do not include
  `viscera/logging`.
- Logging headers and `Logger.cpp` do not include `viscera/error`. Logger failures stay on the logger's counters
  ([logging ADR-003](../logging/adr/ADR-003-no-throw-error-handling.md)).
- `Panic.cpp` writes stderr and aborts. It does not include logging and does not call `flush`.
- A subsystem may include another subsystem's error-enum header. It does not include that subsystem's `.cpp` to
  interpret a code.
- One CMake target: `VisceraEngine`. No separate error library in v1.

## Public API

Namespace for `Error`, `Result`, `ErrorCode`, `ErrorDomain`, `fail`, `panic`, `kMaxErrorFormatFrames`, and
`kMaxErrorBytes`: `viscera`.
Subsystem enums live in the subsystem namespace. Include the umbrella `<viscera/error/error.hpp>`. It does not pull in
a logging header.

`FormatString` is `viscera::FormatString` in `engine/include/viscera/core/FormatString.hpp`
([ADR-004](adr/ADR-004-context-cause-and-origin.md)). `viscera/logging/FormatString.hpp` remains as an alias to that
type so existing logger call sites keep compiling. Move the header in phase 3, the first phase whose signatures name
it, so error headers never include logging.

### Codes ([ADR-002](adr/ADR-002-domains-codes-and-descriptions.md))

`ErrorDomain::Core` is the only v1 enumerator. `domainName` returns `"core"`. The `switch` has no `default`.

An enum becomes an error enum by living in its own namespace and providing `constexpr` `noexcept` `errorDomain` and
`errorMessage`. Values are explicit, start at `1`, and are append-only. `errorMessage` returns static storage.
`CoreError.hpp` is added with the first code that two subsystems share. It is not a place for `Unknown`, `Failed`,
`Internal`, or `InvalidState`.

### `Error`, `Result`, and `fail` ([ADR-003](adr/ADR-003-result-and-propagation.md), [ADR-004](adr/ADR-004-context-cause-and-origin.md))

```cpp
template <typename T>
using Result = std::expected<T, Error>;

inline constexpr int kMaxErrorFormatFrames = 8;
inline constexpr std::size_t kMaxErrorBytes = 256;

template <ErrorEnum E>
[[nodiscard]] std::unexpected<Error> fail(
    E code,
    std::source_location where = std::source_location::current()) noexcept;

template <ErrorEnum E, typename... Args>
[[nodiscard]] std::unexpected<Error> fail(
    E code,
    FormatString<std::type_identity_t<Args>...> format,
    Args&&... args) noexcept;
```

Every function that returns a `Result` is declared `[[nodiscard]]`. The attribute sits on the function. `Result<void>`
success is `return {}`. Engine code checks the result and then uses `*result` or `result->`. It does not call
`.value()`.

`withContext` keeps the code and pushes a frame. `wrap` changes the top-level code and pushes a frame. Both are
`noexcept`. `description()` is the static phrase. `message()` is this frame's context text and may be empty.
`is(code)` tests the top-level code only. `location()` is always present. Constructors are `explicit`. A code-only
`Error` does not allocate. Copy is `noexcept`. `sizeof(Error) <= 256`. `std::bad_alloc` while building a frame calls
`panic`. Any other formatting failure omits the new message text and keeps the code, the static description, and the
origin.

Early return:

```cpp
auto bytes = io::readFile(path);
if (!bytes)
{
    return std::unexpected(
        std::move(bytes).error().withContext("loading level '{}'", levelName));
}
```

There is no propagation macro in v1. `and_then` / `transform` / `or_else` wait until
[ADR-008](adr/ADR-008-testing-and-toolchain-baseline.md) has recorded that every supported standard library provides
them. Examples in this module use the explicit form.

A `Result`-returning function is not `noexcept` unless its body cannot throw or its contract is to abort. Both `fail`
overloads are `noexcept` because allocation failure panics inside them.

### Assertions and panic ([ADR-006](adr/ADR-006-panic-assertions-and-exceptions.md))

`VISCERA_ENABLE_ASSERTS` is defined for Debug and RelWithDebInfo, and left undefined for Release and MinSizeRel.
`VISCERA_PANIC` is always compiled in and calls `panic`. A disabled assert does not evaluate its condition. Do not
assert on file, network, config, or user input.

`panic` formats into a 1024-byte stack buffer, writes the stderr descriptor without iostreams and without allocating,
and calls `std::abort()`. Formatting failure writes a fixed literal instead. `panic` does not call the logger.

## Error text

The formatter writes a single line. Domain brackets use `domainName()` and appear on the first frame and on each later
frame whose code changed (`wrap`). A `withContext` frame adds its message and does not add a new `[domain]` prefix.
Empty messages do not add a colon. The walk is outermost first and prints at most `kMaxErrorFormatFrames` frames.
Past that, the line contains ` ...` and the parenthetical location is the last frame printed. Within the cap, the
parenthetical location is the root origin.

```text
[domain] <static description>[: <message>][: [domain] <description>[: <message>]]* (<file>:<line>)
```

```text
[core] shader compile failed (Shader.cpp:40)

[asset] asset not found: texture 'missing.png' is not in the asset store: [io] file not found: open 'assets/missing.png' failed: No such file or directory (errno 2) (File.cpp:18)
```

The formatter does not mutate the error and does not build a temporary `std::string`. Exceptions from `{fmt}` are
caught and replaced with a short fallback. It does not strip a source-root prefix. That remains the logger's line
formatting.

## Failure classes

Full table: [ADR-006](adr/ADR-006-panic-assertions-and-exceptions.md).

| Class                     | Mechanism                                                             |
|---------------------------|-----------------------------------------------------------------------|
| Normal frequent outcome   | `std::optional` or a status enum                                      |
| Recoverable failure       | `Result`, logged once by the handler                                  |
| Unrecoverable environment | `Result` to `main`: one `Fatal` log, `flush`, shutdown, non-zero exit |
| Allocation failure        | `panic`                                                               |
| Broken invariant          | `VISCERA_ASSERT` or `VISCERA_PANIC`, then `panic`                     |

`logging::Level::Fatal` does not abort ([logging ADR-015](../logging/adr/ADR-015-crash-handling.md)).

## Who logs

[ADR-005](adr/ADR-005-logging-ownership.md). Construction and propagation do not log. The handler logs once, choosing
level and category. The category is the handler's logging category, not a map from `ErrorDomain`. Discarding an error
on purpose logs it at `Debug` with a reason.

Workers return `Result`. The consumer logs. A controlled exit in `main` logs `Fatal`, flushes, shuts down initialized
systems, and returns non-zero. `Engine::version()` stays a plain value.

Foreign codes are translated in the owning `.cpp`. Public headers do not expose them
([ADR-007](adr/ADR-007-boundary-translation.md)).

## Adding a domain later

Do this in the change that introduces the subsystem's first real error enum. Do not add the header ahead of that code.

1. Append an `ErrorDomain` enumerator. Do not renumber, and do not reuse a retired value.
2. Add the `domainName()` arm. Leave the switch without a `default`.
3. Add the `enum class` next to that subsystem's public API. Explicit values, starting at `1`, append-only.
4. Provide `constexpr` `noexcept` `errorDomain` and `errorMessage` in that namespace. Messages are string literals.
5. Translate native failures in the `.cpp`. Keep the native name and number in the message.
6. Return `Result<T>` from operations that can fail, and mark those functions `[[nodiscard]]`.
7. Test the enum contract, the translation, and propagation ([ADR-008](adr/ADR-008-testing-and-toolchain-baseline.md)).
8. Amend the domain list in [ADR-002](adr/ADR-002-domains-codes-and-descriptions.md) in the same change.

`Platform`, `Io`, `Asset`, `Render`, `Audio`, `Input`, and `Game` are the reserved names. A game enum still lives in
game code. The engine does not include it.

## CMake

| Option                   | Role                                                                                                                     |
|--------------------------|--------------------------------------------------------------------------------------------------------------------------|
| `VISCERA_ENABLE_ASSERTS` | On for Debug and RelWithDebInfo. Off for Release and MinSizeRel. Replaces the commented line in `engine/CMakeLists.txt`. |

`VISCERA_PANIC` is not a CMake option. It is always on.

## Implementation sequence

Code follows this order. Each phase is testable on its own. [ADR-008](adr/ADR-008-testing-and-toolchain-baseline.md)
is the acceptance bar.

1. Compile `<expected>` on the `debug`, `debug-gcc`, and `debug-msvc` presets. Record the compiler and the standard
   library. Do not depend on monadic `expected` operations until that check covers them.
2. Identity only: `ErrorDomain`, `ErrorCode`, `ErrorEnum`, `Error` (code, static description, origin), and the
   formatter. No context nodes, no `panic`, no subsystem enums. A test enum proves the concept.
3. Move `FormatString` to `viscera/core` and leave the logging header as a forwarding alias. Then add the `Result`
   alias, `[[nodiscard]]`, and `fail`. Compile tests for `Result<int>`, `Result<void>`, `return {}`,
   `std::unexpected`, and a move-only success type. No subclass and no macro.
4. `panic`, asserts, and the CMake definition above. Death test with the logger never initialized. Context construction
   calls `panic` on `bad_alloc`, so the abort path exists before frames do.
5. Context chain, `withContext`, `wrap`, the eight-frame formatter cap, and the `bad_alloc` path into `panic`.
6. Logger capture tests. No change to the logging call API and no change to logging ADR-003's decision. Confirm the
   forwarding `FormatString` header still compiles existing logger call sites, and that logging still does not include
   the error module.
7. The first real `Result`-returning engine operation is a future initialization entry point. `main` owns the failure
   decision. `Engine::version()` is unchanged.
8. Subsystem error enums arrive with their subsystems, using the checklist above.

### Intended layout

Created by those phases, not by this documentation:

```text
engine/include/viscera/core/FormatString.hpp
engine/include/viscera/logging/FormatString.hpp      forwarding alias
engine/include/viscera/error/error.hpp               umbrella
engine/include/viscera/error/ErrorDomain.hpp         ErrorDomain, domainName
engine/include/viscera/error/ErrorCode.hpp           ErrorCode, ErrorEnum
engine/include/viscera/error/ErrorType.hpp           Error, formatter, kMaxErrorFormatFrames, kMaxErrorBytes
engine/include/viscera/error/detail/Node.hpp         internal; included from ErrorType.hpp, not the umbrella
engine/include/viscera/error/Result.hpp              Result<T>, fail
engine/include/viscera/error/Assert.hpp
engine/include/viscera/error/Panic.hpp
engine/src/error/Error.cpp
engine/src/error/Panic.cpp
engine/tests/src/error/                              unit tests and death tests
engine/tests/compile/error_standalone.cpp            umbrella header, no logging include
```

`Error` lives in `ErrorType.hpp`. `error.hpp` and `Error.hpp` are the same path on Windows, so the class cannot use
that filename and still leave room for the umbrella.

`CoreError.hpp`, `io/IoError.hpp`, and the other subsystem enums are not part of v1. They are added by the checklist.

## Diagrams

PlantUML sources. Render with an IDE plugin or the PlantUML CLI.

| File                                                              | Description                                                            |
|-------------------------------------------------------------------|------------------------------------------------------------------------|
| [error-classes.puml](diagrams/error-classes.puml)                 | `Error`, `ErrorCode`, `ErrorDomain`, context node, `Result`, formatter |
| [error-dependencies.puml](diagrams/error-dependencies.puml)       | Allowed includes, and the edges that must stay absent                  |
| [failure-handling-flow.puml](diagrams/failure-handling-flow.puml) | Which mechanism to use, and which layer logs                           |
| [propagation-sequence.puml](diagrams/propagation-sequence.puml)   | One warning at recovery, one fatal at `main`, panic off the logger     |

## v1 non-goals

Kept out on purpose, so implementation does not add them "while we are here":

- Propagation macros, including `VISCERA_TRY` and assignment or context variants.
- `hasCause` / `causedBy`. `is()` stays top-level.
- A subclass of `std::expected`.
- Severity, stack traces, user-facing text, or `std::any` on `Error`.
- A separate CMake target for the error module.
- Building the engine with `-fno-exceptions`.
- `Result` returns from logging calls.
- Empty subsystem error headers, and a `Game` enumerator before game code has an error enum.
- A replaceable `panic` handler.
- Logging from `panic`, including `Fatal` plus `flush`.

## ADRs

- \subpage adr_err_001 — One `viscera::Error` for recoverable failures. Logging stays a separate leaf.
- \subpage adr_err_002 — Closed domains, subsystem-owned codes, static descriptions captured onto the value.
- \subpage adr_err_003 — `Result` is `std::expected<T, Error>`. Propagation is explicit. No macro in v1.
- \subpage adr_err_004 — Origin, `withContext`, `wrap`, immutable frames, formatter grammar, shared `FormatString`.
- \subpage adr_err_005 — The handler logs once. The logger does not start returning `Result`.
- \subpage adr_err_006 — Assertions, stderr `panic`, exceptions at boundaries, allocation failure aborts.
- \subpage adr_err_007 — Foreign codes are translated in the owning `.cpp` and stay out of public headers.
- \subpage adr_err_008 — `<expected>` baseline, tests, and the acceptance checklist.
