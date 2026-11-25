# ADR-008: Testing and toolchain baseline {#adr_err_008}

| Field        | Value                                                                                                         |
|--------------|---------------------------------------------------------------------------------------------------------------|
| **Status**   | Accepted                                                                                                      |
| **Date**     | 2026-11-24                                                                                                    |
| **Deciders** | Cas Rovers                                                                                                    |
| **Module**   | `error` (`viscera`)                                                                                           |
| **Related**  | Depends on: ADR-001, ADR-002, ADR-003, ADR-004, ADR-005, ADR-006, ADR-007, logging ADR-016 · Supersedes: none |

## Context

`std::expected` is a C++23 library facility. Compiler version alone does not mean the selected standard library
provides it. A Clang that reports an older `__cpp_concepts` value can fail to declare `std::expected` in libstdc++.
The project does not pin a compiler or a standard library. The first implementation step is a compile check, not a
public header that assumes the feature.

The error module is on the path of every future subsystem. It needs stronger compile-time and behavioural coverage
than a leaf feature. Tests use the existing GoogleTest layout under `engine/tests/`, the same stance as
[logging ADR-016](../../logging/adr/ADR-016-testing-and-benchmarking.md): behaviour is in the test binary, and this
module does not add a benchmark gate.

Nothing in this ADR is a claim that a prototype already passed these checks. The checks are the acceptance bar for the
implementation.

## Decision

**Implementation starts by compiling `<expected>` on every debug preset. The module is ready to use when the checks
below pass on Clang, GCC, and MSVC, and when the README, these ADRs, and the diagrams still describe the code.**

Details:

1. **Baseline check, before public headers depend on `<expected>`.** A CMake compile check includes `<expected>` and
   `<version>`, verifies `__cpp_lib_expected`, default-constructs `std::expected<int, int>`, and constructs
   `std::unexpected`. Run it through the `debug`, `debug-gcc`, and `debug-msvc` configure presets where those
   toolchains are available. Record the compiler version and the standard-library identity (libstdc++, libc++, or the
   Microsoft STL) in the build notes for that change. The portability baseline is `expected` / `unexpected` and the
   observers `operator bool`, `operator*`, and `error()`. Monadic operations (`and_then`, `transform`, `or_else`) are
   not required, and call sites do not use them, until this check has passed for those operations on all three
   presets ([ADR-003](ADR-003-result-and-propagation.md)).
2. **Unit tests** live under `engine/tests/src/error/` and cover:

    - `ErrorCode` equality, domain distinction, and stable numeric values.
    - `ErrorEnum`: a valid test enum is accepted; a signed underlying type, a type wider than `uint16_t`, an unscoped
      enum, and a missing ADL hook are rejected.
    - `Error`: code, static description, source location on a code-only value, copy, and move. Copy of a code-only
      error does not allocate.
    - Context: `withContext` keeps the code; `wrap` changes the top-level code; `cause()` walks to the previous error;
      `is()` matches the top-level code only.
    - Formatting: code-only, one message, several contexts, nested causes, the eight-frame cap, and the ` ...` marker.
      `kMaxErrorFormatFrames` is asserted equal to 8.
    - `Result`: value success, `Result<void>` success via `return {}`, error propagation, and a move-only success type.
    - `fail`: code-only and formatted construction, with the source location equal to the call site rather than the
      header.
    - Panic: aborts, writes a diagnostic, writes the fallback literal when formatting cannot finish, and does both
      when the logger has never been initialized. Implemented as GoogleTest death tests. The death test does not call
      `logging::flush`.
    - Assertions: with `VISCERA_ENABLE_ASSERTS`, a failing assert aborts. Without it, the condition expression is not
      evaluated (a side effect inside the condition does not run).
    - Logger integration, using the existing memory sink: an `Error` is copied and destroyed by capture; the formatter
      does not mutate nodes; a handled error produces one record; creation and propagation produce none.

3. **Compile tests** under `engine/tests/compile/` include a standalone translation unit that includes
   `<viscera/error/error.hpp>` and does not include a logging header. That unit builds `Error`, `Result<int>`,
   `Result<void>`, a move-only `Result`, `fail`, and `fmt::format("{}", error)`. The include-direction CTest scans this
   file so a logging include cannot sneak in. A compile-fail test discards a `[[nodiscard]]` `Result` and expects the
   build to fail on Clang, GCC, and MSVC under the warnings-as-errors presets. A second compile-fail test passes a
   mismatched format string to `fail`, `withContext`, `wrap`, and the formatted `Error` constructor. clang-tidy and
   cppcheck are additional checks. A clean tidy run does not excuse a missing `[[nodiscard]]`.
4. **Include-direction checks fail the build if violated.** A CTest (a grep or an equivalent source scan) fails when:

    - a file under `engine/include/viscera/logging/` or `engine/src/logging/` includes `viscera/error`;
    - a file under `engine/include/viscera/error/` includes `viscera/logging`;
    - `engine/src/error/Panic.cpp` includes `viscera/logging`;
    - `engine/include/viscera/core/FormatString.hpp` includes `viscera/logging` or `viscera/error`.

   The logger must format an `Error` without a logging translation unit including an error header. `{fmt}` finds
   `fmt::formatter<viscera::Error>` when the caller who passes the `Error` has included the error header. That is the
   same rule logging ADR-008 uses for every other user-defined type.
5. **Integration cases for exactly-once logging.**

    - A lower-level function returns an error and does not log.
    - An intermediate layer adds context and does not log.
    - A handler recovers and logs one warning.
    - A top-level handler logs one fatal record. The test may flush. Production `main` also shuts down and returns
      non-zero ([ADR-005](ADR-005-logging-ownership.md)).
    - A propagated error is not logged twice.
    - A worker thread returns an `Error` to a consumer thread, which inspects it and logs it once.

6. **Acceptance.** The implementation matches this module's README, ADRs, and diagrams, and all of the following are
   true:

    - Every `Result`-returning API is explicitly `[[nodiscard]]`.
    - The error core compiles and links without logging headers and without a logging link dependency.
    - Error identity is the stable subsystem-owned code from [ADR-002](ADR-002-domains-codes-and-descriptions.md).
    - Code-only errors do not allocate.
    - Context frames are immutable, and `Error` is nothrow-copyable and no larger than 256 bytes.
    - The formatter works through the real logger capture path.
    - Creating and propagating an error does not log. Handling it logs once.
    - `panic` does not depend on logger initialization, a console sink, or `flush`.
    - Recoverable environment failures use `Result`. Invariants and allocation failure use assert or `panic`.
    - Foreign failures are translated at the owning `.cpp` ([ADR-007](ADR-007-boundary-translation.md)).
    - The facilities used by the headers have been built and tested on the Clang, GCC, and MSVC debug presets.

   A behaviour change updates the ADR or the README in the same change. Diagrams that show the changed rule move with
   it.

## Alternatives considered

### Trust the language flag and skip the `<expected>` check

`cxx_std_23` is already required on `VisceraEngine`. It was not chosen because a C++23 language mode does not guarantee
that the linked standard library implements `std::expected`. The check is cheap and it records which library was
actually compiled.

### A custom test harness instead of GoogleTest

Death tests and the existing logger fixtures are already on GoogleTest. A second harness was not chosen. Logging
ADR-016 is the project test policy.

### Allocation-failure injection as a v1 gate

A failable allocator would prove the `bad_alloc` path on every run. It was not chosen as a gate. The `noexcept`
constructors must catch `bad_alloc` and call `panic` (ADR-004, ADR-006). A death test that simulates that call is
enough for v1. A global allocation shim waits until a test can install one without disturbing the logger and GoogleTest.

## Consequences

### Positives

- A standard library without `std::expected` fails at configure time, with a recorded compiler and library version.
- The dependency direction is a test, not a review comment that can be missed.
- Exactly-once logging and the uninitialized-logger panic are both executable checks.

### Negatives

- Death tests and compile-fail tests take more care than ordinary `EXPECT`s. They are still the right tool.
- The include scan is a textual check. A macro that hides the include could slip past a naive grep. The scan should
  look for the include path itself, and review still looks at new headers.
- Three presets are the bar. A machine that only has Clang still has to treat the GCC and MSVC runs as part of
  finishing the work, not as optional.

### Revisit when

The three debug presets disagree on `std::expected` or on `[[nodiscard]]`. The response is to narrow the portable
subset in [ADR-003](ADR-003-result-and-propagation.md), or to fix the toolchain, before adding more error API.
