# ADR-003: Result and propagation {#adr_err_003}

| Field        | Value                                                               |
|--------------|---------------------------------------------------------------------|
| **Status**   | Accepted                                                            |
| **Date**     | 2026-11-24                                                          |
| **Deciders** | Cas Rovers                                                          |
| **Module**   | `error` (`viscera`)                                                 |
| **Related**  | Depends on: ADR-001, ADR-002, foundation ADR-002 · Supersedes: none |

## Context

[ADR-001](ADR-001-engine-wide-error-model.md) fixes `Error` as the failure value. Call sites still need a way to return
success or failure, a way to build that failure, and a way to return early. C++23 provides `std::expected`. It does not
provide a `?` operator.

Four constraints narrow the API:

- The standard type is not a customization base. Monadic operations on a derived type return `std::expected`, not the
  derived type.
- A typedef cannot portably stamp `[[nodiscard]]` onto every use of the underlying type. Ignored results have to be
  diagnosed on Clang, GCC, and MSVC ([foundation ADR-002](../../../foundation/adr/ADR-002-clang-gcc-msvc.md)).
- Extensions are off. A GNU statement-expression `TRY` macro will not compile on this project.
- `.value()` throws `std::bad_expected_access`. Engine control flow does not use exceptions
  ([ADR-006](ADR-006-panic-assertions-and-exceptions.md)).

`std::expected` itself has to be real on every supported compiler and standard-library pair before headers depend on
it. That check is [ADR-008](ADR-008-testing-and-toolchain-baseline.md). The signatures below use only the basic
`expected` / `unexpected` API.

## Decision

**`Result<T>` is an alias of `std::expected<T, Error>`. Every function that returns a `Result` is marked
`[[nodiscard]]`. Early return is written out with `std::unexpected`. v1 has no propagation macro.**

Details:

1. **The alias is:**

    ```cpp
    template <typename T>
    using Result = std::expected<T, Error>;
    ```

   `Result<void>` is the no-value form. Success is `return {};`. The alias lives in `namespace viscera`, in
   `Result.hpp`, and is included by `<viscera/error/error.hpp>`.
2. **Do not subclass `std::expected`.** A derived wrapper would exist only to hang an attribute on the type. Inherited
   `and_then` and `transform` would return the base, and the wrapper would grow its own constructors and assignment
   operators. The public signatures stay the standard ones.
3. **`[[nodiscard]]` is on the function, not on the alias.** A function that returns `Result<T>` and is part of the
   engine API is declared `[[nodiscard]]`. That includes public headers and internal functions whose result is a
   failure the caller must forward or handle. clang-tidy and cppcheck may warn as well. They do not replace the
   attribute. [ADR-008](ADR-008-testing-and-toolchain-baseline.md) requires a discarded result to fail the build on all
   three compilers, because warnings are errors in the presets.
4. **Engine control flow does not call `.value()`.** Check the result, then use `*result` or `result->`. Dereferencing
   an error-state `expected` violates its precondition. Standard-library hardening may catch that in a debug build. It
   is not the check.
5. **Propagation is explicit:**

    ```cpp
    auto bytes = io::readFile(path);
    if (!bytes)
    {
        return std::unexpected(
            std::move(bytes).error().withContext("loading level '{}'", levelName));
    }
    ```

   `withContext` and `wrap` are defined in [ADR-004](ADR-004-context-cause-and-origin.md). Moving the error out of a
   failed result is the form to copy. There is no `fail(Error)` overload. `fail` builds a new error. An error that
   already exists is passed to `std::unexpected` directly.
6. **`and_then`, `transform`, and `or_else` are for a short pure transformation.** They are not a way to hide a sequence
   of early returns inside lambdas. Call sites use them only after the ADR-008 baseline has recorded that every
   supported standard library actually provides those operations. Until that record exists, call sites use the explicit
   form only. The examples in the module README use the explicit form.
7. **`fail` has two overloads, both `noexcept`.**

    ```cpp
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

   The code-only overload's defaulted `std::source_location` is last, so it is evaluated at the caller. The formatted
   overload takes `viscera::FormatString`, which already carries the call site
   ([ADR-004](ADR-004-context-cause-and-origin.md)). It does not take a `std::source_location` before the format
   string. A defaulted location placed before a parameter pack would bind inside `fail` or would not compile. `fail`
   forwards the captured location into `Error`. It does not default the location a second time.
8. **A `Result`-returning function is not `noexcept` by default.** `std::expected` does not make the function body
   non-throwing. Allocation and third-party calls can still throw `std::bad_alloc`. A `noexcept` marker would turn that
   into `std::terminate` before a boundary can report it. `noexcept` is limited to the cases in
   [ADR-006](ADR-006-panic-assertions-and-exceptions.md): the path really cannot throw, or termination is the contract.
   Both `fail` overloads are `noexcept` because a context allocation failure panics inside them rather than escaping.
9. **v1 has no `VISCERA_TRY`, no assignment macro, and no context macro.** A `do { } while (false)` macro cannot leave
   a declared value in the caller's scope. That shape is rejected up front, not deferred as a second macro to design
   later.

## Alternatives considered

### Subclass `std::expected` so the type itself is `[[nodiscard]]`

A derived `Result` could carry the attribute once. It was not chosen because monadic operations return `std::expected`,
constructors and conversion become a second API, and an alias plus a function attribute keeps call sites on the
standard type. A wrapper is worth another ADR only if all three compilers fail to diagnose an ignored result even with
`[[nodiscard]]` on the function and warnings-as-errors. The first response to that failure is the compiler and
static-analysis setup, not a wrapper.

### Four macros for early return, assignment, and context

Macros can approximate a `?` operator. It was not chosen because C++ has no hygienic form that is portable with
extensions disabled, and because macros complicate debugging, static analysis, and scope. One `VISCERA_TRY` may be
proposed later for a single early-return shape. `_CTX` and `_ASSIGN` variants are not part of that proposal.

### Monadic chains as the only style

`and_then` expresses a pure pipeline well. It was not chosen as the default because engine code is mostly imperative,
and a long chain of lambdas is harder to step through than an early return.

## Consequences

### Positives

- Signatures match `std::expected`. There is no second result type to learn.
- Ignored failures are a compile error where the attribute is present and warnings are errors.
- Propagation is visible in the debugger. There is no macro to expand.
- `fail`'s location is the caller's line, which is the origin [ADR-004](ADR-004-context-cause-and-origin.md) stores.

### Negatives

- Every `Result` function repeats `[[nodiscard]]`. A reviewer has to notice a missing attribute. The compile-fail test
  in ADR-008 covers a sample. It cannot see every future function by itself.
- Explicit propagation is longer than a `?` operator. That length is accepted until real subsystems show it is a
  problem.
- `fail` returning `std::unexpected<Error>` means a function declared `Result<T>` relies on converting
  `unexpected<Error>` into `expected<T, Error>`. That conversion is the standard one. Callers should not need a manual
  `Result<T>` construction for the failure path.

### Revisit when

The same early-return boilerplate is a demonstrated readability problem across more than one subsystem. The follow-up
may add one `VISCERA_TRY` macro, with compile tests on Clang, GCC, and MSVC. That follow-up does not add assignment or
context macros in the same change. Also revisit if a supported standard library still cannot diagnose an ignored
`[[nodiscard]]` `Result` after the attribute is in place.
