# ADR-004: Context, cause, and origin {#adr_err_004}

| Field        | Value                                                                                                                        |
|--------------|------------------------------------------------------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                                                                     |
| **Date**     | 2026-11-24                                                                                                                   |
| **Deciders** | Cas Rovers                                                                                                                   |
| **Module**   | `error` (`viscera`)                                                                                                          |
| **Related**  | Depends on: ADR-001, ADR-002, ADR-003, logging ADR-004, logging ADR-005, logging ADR-008, logging ADR-011 · Supersedes: none |

## Context

A code says what failed. It does not say which file was being opened, which texture was being decoded, or which earlier
failure caused this one. [ADR-002](ADR-002-domains-codes-and-descriptions.md) stores the static description on the
`Error` because the enum type is erased. The origin has the same problem: if a code-only error drops its
`std::source_location`, the log line cannot say where the failure was born.

The logger will copy an `Error` by value into a 256-byte slot and format it later on the backend thread
([logging ADR-008](../../logging/adr/ADR-008-custom-type-formatting.md),
[logging ADR-011](../../logging/adr/ADR-011-allocation-policy.md)). The copy must be `noexcept`. The formatter must not
throw, must not mutate the value, and must not allocate on the producer's behalf. Context messages also need a
compile-time-checked format string and a caller location. Logging already has that wrapper as
`viscera::logging::FormatString` ([logging ADR-004](../../logging/adr/ADR-004-compile-time-format-checking.md),
[logging ADR-005](../../logging/adr/ADR-005-source-location-capture.md)). Error headers cannot include logging to use
it ([ADR-001](ADR-001-engine-wide-error-model.md)).

Context is a cold path. A failure that happens every frame is a `std::optional` or a status enum
([ADR-006](ADR-006-panic-assertions-and-exceptions.md)), not a chain of allocated frames.

## Decision

**Every `Error` stores its code, static description, and frame location. `withContext` keeps the code and pushes a
frame. `wrap` changes the code and pushes a frame. The chain is an immutable shared node. The formatter prints one
line and at most eight frames.**

Details:

1. **The stored facts stay distinct.**

   | Fact               | Where it lives                                             | Example                                       |
   |--------------------|------------------------------------------------------------|-----------------------------------------------|
   | Code               | `ErrorCode` on this frame                                  | `Asset` + `NotFound`                          |
   | Static description | `std::string_view` captured from `errorMessage`            | `"asset not found"`                           |
   | Frame location     | `std::source_location` captured at this public constructor | the `fail` or `withContext` call              |
   | Context message    | Optional immutable frame                                   | `"texture 'missing.png' is not in the store"` |
   | Cause              | The previous `Error`, linked from that frame               | the `io` error this asset error wraps         |

   The root origin is the frame location of the innermost `Error` (walk `cause()` until null). When the chain is within
   the formatter cap, the rendered line prints that root location once. Past the cap, point 7 says which location is
   printed. `location()` on a given `Error` returns this frame's location and is always present. A missing location is
   not a representation this design uses.

2. **Operations.**

    - `withContext(format, args...)` is `noexcept`. It keeps `code()` and the static description, records a new frame
      location, and stores the previous error as the cause.
    - `wrap(newCode, format, args...)` is `noexcept`. It replaces the top-level code and static description, records a
      new frame location, and stores the previous error as the cause.
    - Plain propagation does not call either function. The value is moved unchanged.
    - `description()` returns the static description view.
    - `is(code)` compares the top-level `ErrorCode` only. It does not walk the cause chain.
    - `cause()` returns the previous `Error`, or null at the root. The pointer stays valid for the lifetime of the
      `Error` it came from.
    - `message()` returns this frame's context message, or an empty view when the frame has none. That view stays valid
      for the lifetime of this `Error`. It is not the static description.

   The `Error` constructors are `explicit`, so a subsystem enum does not convert to an `Error` by accident. `fail` is
   the helper that builds a `std::unexpected<Error>`.

   v1 has no `hasCause` or `causedBy`. A later helper may walk the chain. It does not change what `is()` means. Only
   `wrap` when the higher layer has a different abstraction and a different caller reaction. Extra operational detail
   uses `withContext`.

3. **Representation.** A code-only `Error` stores `m_code`, `m_description`, and `m_location`, and a null node. It does
   not allocate. A frame is a `std::shared_ptr<const Node>`. The node holds the message string and the cause `Error`.
   Frame location lives on the `Error` handle (`m_location`), including for framed errors. Nodes are immutable after
   construction. Copying an `Error` copies the code, the description view, the location, and the `shared_ptr` (a
   reference-count bump). Copy and move are `noexcept`. Destruction of a moved-from `Error` is safe. Using it otherwise
   is not part of the contract.
4. **The handle fits the logger slot.** `static_assert(sizeof(Error) <= 256)` on every supported ABI. The type is
   nothrow-copyable. A specific `sizeof` beyond that bound is not a promise. Tests measure it and pass an `Error`
   through the real logger capture path ([ADR-008](ADR-008-testing-and-toolchain-baseline.md)).
5. **Allocation failure panics.** Building a frame may allocate the node and the message string. Any exception on that
   path, including `std::bad_alloc`, is caught inside the `noexcept` `attachFrame` helper and calls `panic()`
   ([ADR-006](ADR-006-panic-assertions-and-exceptions.md)). The function does not drop the new frame and return as if
   the context had been recorded, and it does not let the exception escape. Catching `...` rather than only
   `std::bad_alloc` keeps a `noexcept` constructor from `std::terminate` on a surprise throw.
6. **Any other formatting failure omits the new message text and keeps the identity.**

    - A formatted `Error` or `fail` becomes a code-only error with the requested code, its static description, and the
      call-site location.
    - `withContext` returns a copy of the existing error and does not push a frame.
    - `wrap` still switches to the new code and static description and still links the old error as the cause. Only the
      failed message text is absent.

   Linking a cause allocates a node. That allocation is the `bad_alloc` path in point 5, not this one. This point is
   for a format error that is not out-of-memory. The implementation does not invent a fallback sentence.
7. **Construction does not cap depth.** The chain is acyclic because each new frame's cause is the previous value, not
   itself. The formatter still guards. `viscera::kMaxErrorFormatFrames` is `8`. The walk is outermost frame first, then
   each cause, and it prints at most eight frames. A longer chain stops after eight frames. The marker ` ...` follows
   the last printed frame, and the parenthetical location is that frame's location. The constant is public so tests
   can assert it.
8. **The formatter is `fmt::formatter<viscera::Error>`.** It writes to the output iterator, does not mutate the `Error`
   or its nodes, and does not build a temporary `std::string` to hand to the logger. Exceptions from `{fmt}` are caught
   and replaced with a short fallback so the formatter does not throw. Domain prefixes use `domainName()` and appear
   only on a frame whose code differs from the previous frame printed (the first frame always prints one). A
   `withContext` frame adds `: <message>` and no new `[domain]` prefix. An empty message does not add an extra colon.

   Grammar, outermost first:

   ```text
   [domain] <static description>[: <message>][: [domain] <description>[: <message>]]* (<root-file>:<line>)
   ```

   A code-only error is `[domain] <static description> (<root-file>:<line>)`. Within the frame cap, the parenthetical
   file and line are the innermost frame's location. Point 7 covers a longer chain. Paths are printed as the
   `std::source_location` file name. Stripping a source-root prefix is the logger's job when the line is a log line,
   not the error formatter's job.

   Examples:

   ```text
   [core] shader compile failed (Shader.cpp:40)

   [asset] asset not found: texture 'missing.png' is not in the asset store: [io] file not found: open 'assets/missing.png' failed: No such file or directory (errno 2) (File.cpp:18)
   ```

   In the second line, `asset` wrapped an `io` error. The location is the `io` origin, not the wrap site.
9. **`FormatString` moves to `viscera::FormatString` in `engine/include/viscera/core/FormatString.hpp`.** The type keeps
   the behaviour in logging ADR-004 and ADR-005: a `consteval` constructor, compile-time format checking, and
   `std::source_location::current()` defaulted so the caller is what gets stored. The header depends only on `{fmt}`
   and the standard library. `engine/include/viscera/logging/FormatString.hpp` remains, and becomes a forwarding alias:

   ```cpp
   namespace viscera::logging
   {
       template <typename... Args>
       using FormatString = ::viscera::FormatString<Args...>;
   }
   ```

   Existing logger call sites keep compiling. Error headers include the core header only. Do the move in the same
   phase that first names `FormatString` from an error header (phase 3 in the module README), so that header never
   includes `viscera/logging`. Phase 6 then passes an `Error` through the real logger capture path and checks that
   logging still does not include the error module. This ADR records the type now so the `fail` signature in
   [ADR-003](ADR-003-result-and-propagation.md) has a name.

Context belongs on cold paths: a missing asset, a failed device call, a bad config value. It is not how a frame loop
reports "queue was empty".

## Alternatives considered

### Null location on a code-only error

Skipping `std::source_location` when the caller passed no message keeps the handle smaller. It was not chosen because
the origin is useful exactly when nobody wrote a sentence, and `source_location` is two pointers plus two integers of
static data, not a heap allocation.

### Drop the new frame on `bad_alloc` and continue

The caller would still see the original code, with the context sentence missing. It was not chosen because out-of-
memory is fatal in [ADR-006](ADR-006-panic-assertions-and-exceptions.md). A context path that swallows `bad_alloc`
would contradict that policy.

### A fixed fallback sentence when formatting fails

A frame such as `"context unavailable"` would keep the chain shape. It was not chosen because the sentence is not
something the caller wrote, and it can be mistaken for a real operation name. The identity is kept. The failed text is
left out.

### Cap the chain at construction

Refusing the ninth `withContext` would bound memory. It was not chosen because the cap is a diagnostic limit. The
formatter enforces it. Construction stays a faithful record of what the callers did.

### A central `describe(ErrorCode)` instead of a stored view

Rejected in [ADR-002](ADR-002-domains-codes-and-descriptions.md). Repeated here only because the formatter must not
grow that switch.

## Consequences

### Positives

- A log line can show the root origin, the static meaning, and the operations that wrapped it, in one `{}` argument.
- Copies are safe across threads. Frames are immutable, so a worker can return an `Error` to the thread that logs it.
- Code-only failures stay allocation-free.
- Logging and errors share one format-string wrapper without error headers including the logger.

### Negatives

- A context frame allocates. Callers who sprinkle `withContext` on a hot path will allocate on that path.
- `bad_alloc` while adding context terminates the process, including when the original error was recoverable.
- `sizeof(Error)` includes a `shared_ptr` and a `source_location`. It has to be watched against the 256-byte slot.
- The formatter prints one location, the root. Intermediate frame locations are stored and are available through
  `location()` and `cause()`. They do not appear in the single-line rendering.

### Revisit when

Measured `Error` values no longer fit the logger slot, or real chains regularly pass eight frames and the truncation
marker hides the root cause. Raising `kMaxErrorFormatFrames` is a one-line contract change in this ADR. Changing the
handle representation is a new decision.
