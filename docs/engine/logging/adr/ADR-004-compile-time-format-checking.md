# ADR-004: Compile-time format string checking

| Field        | Value                                                    |
|--------------|----------------------------------------------------------|
| **Status**   | Accepted                                                 |
| **Date**     | 2026-11-18                                               |
| **Deciders** | Cas Rovers                                               |
| **Module**   | `logging` (`viscera::logging`)                           |
| **Related**  | Depends on: ADR-002, ADR-005, ADR-006 · Supersedes: none |

## Context

Every log call passes a format string and a list of arguments, for example `"Loaded {} in {} ms"` with a name and a
duration. Mistakes in this pairing are easy to make: a missing or extra `{}`, a wrong format specifier, or an argument
type that cannot be formatted. With a runtime-checked format string, such a mistake is only found when the line
executes.

For a logger that is a particularly bad failure mode:

- **Log lines on rare paths are the ones that get written least often.** Error and recovery messages can sit unexecuted
  for months, so a typo in one is found at the worst possible moment.
- **Formatting happens later, on the backend thread** (ADR-001), far from the call site that made the mistake. A runtime
  failure is detached from its cause.
- **Runtime failures have no good outlet.** Logging calls never throw and never report errors to the caller (ADR-003),
  so a bad format string can only degrade into a diagnostic record or a counter increment, not a visible error at the
  call site.

`{fmt}` (ADR-002) can validate the format string against the argument types at compile time, using a format string type
whose constructor is evaluated at compile time. This needs the logging functions to receive the format string as that
type, not as a plain `std::string_view`. The logging functions are templates over the argument types
(`template <typename... Args>`), which is what makes this possible: the argument types are known when the format string
is checked. It also places a requirement on every function in the chain, including the convenience functions (`trace()`,
`debug()` and so on) that forward to `log()`: if any of them takes the format string as a plain string and forwards
that, the check is lost.

## Decision

**The logging API will only accept format strings that are checked at compile time, and a mismatch between format string
and arguments is a compile error.**

Details:

1. **`log()` and every convenience function take the format string as a compile-time-checked `{fmt}` format string
   type**, templated on the argument types. The public signatures, including the ADR-005 wrapper and the ADR-007
   category parameter, are the ones in ADR-006. This ADR only requires that the format parameter is the checked type (or
   the wrapper that holds it), never a plain `std::string_view`.

2. **Convenience functions forward the format-string wrapper itself**, not a string view extracted from it, so the check
   is applied once at the outermost call and is never bypassed. A convenience function that took the format string as
   plain text would silently remove the check, so this is a rule for the whole module.
3. **The public API has no runtime format string path.** Callers cannot pass a format string built at runtime. When the
   text of a message is only known at runtime, it is logged as an argument to a constant format string (for example
   `"{}"`). The **backend** is an internal exception: it formats from a stored pointer and a format thunk, so it uses
   `fmt::vformat` / `fmt::runtime` (ADR-008). That does not reopen a runtime path for call sites.
4. **Argument types must be formattable at compile time.** Passing a type with no formatter is a compile error, which
   ties this decision to ADR-008, where the set of loggable types is defined.
5. **Because format strings are compile-time constants, the record can refer to them without copying.** The captured
   record (ADR-001) can hold a view of the format string instead of an owned copy, which keeps the producer path cheap
   and supports the allocation policy (ADR-011). The API contract is that format strings are string literals or other
   constants with static storage duration.
6. **The check relies on compiler support for compile-time evaluation of the format string** (C++20 `consteval`,
   available in the C++23 baseline of the project). A toolchain that cannot provide it is outside the supported set for
   this module.

## Alternatives considered

### Runtime checking only

Format strings are plain `std::string_view` parameters, and mistakes surface when the line is formatted. This gives the
simplest signatures and permits runtime-built format strings. It was not chosen because errors would appear only when
the line runs, often on rarely executed paths, and in a different thread from the cause. With ADR-003 in place they
would also be reduced to a counter or a diagnostic record instead of a clear failure.

### Wrapping each format string in a macro at the call site

`{fmt}` provides a macro that turns a string literal into a checked format string, which was the way to get compile-time
checking before C++20. It was not chosen because it forces every call site to carry the macro, and any call site that
forgets it is silently unchecked. With `consteval` available this is unnecessary.

### Compile-time checking with an opt-in runtime escape hatch

The checked path is the default, but callers can explicitly mark a format string as runtime (`fmt::runtime`) when they
need one. This is flexible and covers cases like format strings loaded from localization data. It was not chosen because
it reintroduces runtime-only failures through a side door, and because a runtime string breaks the static-storage
assumption in point 5, forcing the record to copy it. The need for it is speculative; it can be added later as a
separate, clearly named function.

### Checking through a macro-based API

The check would be done by a macro that expands to a `static_assert` over the format string. This could work with any
compiler, but it was not chosen because it ties the checking mechanism to the macro-versus-function decision in ADR-006
and gives worse diagnostics than the library's own check.

## Consequences

### Positives

- Format string mistakes and unformattable argument types are caught at build time, at the offending call site.
- Rare and error-path log lines are checked as well as frequent ones, so they cannot hide a latent mistake.
- Fewer failure cases remain at runtime, so the backend thread has less to defend against (ADR-003).
- Format strings can be stored by reference in the record, reducing producer-side work and allocation (ADR-001,
  ADR-011).
- The checked signature is the same for every call, so there is one consistent way to log.

### Negatives

- Every convenience function in the chain must use the checked format string type, and one that does not breaks the
  guarantee silently. This needs a review rule and tests (ADR-016).
- Format strings cannot be built or loaded at runtime, so localized or data-driven message text needs a workaround.
- The template-based API instantiates a function per combination of argument types, which adds to compile time and code
  size.
- The format string type and its header appear in the public logging headers, so `{fmt}` is part of the module's public
  interface. Call sites still do not write `#include <fmt/...>`; they include the logging headers (ADR-002).
- A trailing parameter after the argument pack is not possible in C++, so capturing the source location (ADR-005) cannot
  simply be a defaulted last parameter. It must be solved in a way that keeps this check intact.
- Compile errors from a failed check can be long and unfamiliar to people new to the library.

### Revisit when

A real need appears for runtime-built or externally loaded format strings (for example localization), or a supported
toolchain cannot evaluate the format string check at compile time.
