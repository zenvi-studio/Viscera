@page adr_log_002 ADR-002: Formatting library: fmt vs std::format

# ADR-002: Formatting library: `{fmt}` vs `std::format`

| Field        | Value                                                                     |
|--------------|---------------------------------------------------------------------------|
| **Status**   | Accepted                                                                  |
| **Date**     | 2026-11-18                                                                |
| **Deciders** | Cas Rovers                                                                |
| **Module**   | `logging` (`viscera::logging`)                                            |
| **Related**  | Depends on: ADR-001, foundation ADR-005 (FetchContent) · Supersedes: none |

## Context

The logger formats user-supplied arguments into text on the backend thread (ADR-001). That needs a type-safe formatting
facility with extensible support for engine types (ADR-008) and compile-time format string checking (ADR-004).

The engine targets C++23, and two candidates exist:

- **`std::format`** (C++20, extended in C++23 and C++26). It needs no external dependency, but its feature set is tied
  to the language standard, and its completeness and behaviour depend on which standard library implementation a given
  toolchain ships (MSVC STL, libstdc++, libc++).
- **`{fmt}`** is the library `std::format` was standardised from. It is a third-party dependency that moves faster than
  the standard and includes features that have only been added to the standard recently, or are still being added.

Constraints that shape the choice:

- **Same behaviour on every build and platform.** The engine is built with different compilers and standard libraries.
  The log output, the supported features and any formatting bugs should not change because of the toolchain.
- **Features available now.** Some facilities the logger benefits from are only in C++26 `std::format`, or not in the
  standard at all, while the project is on C++23. Waiting on standard library support is not an option.
- **Deferred formatting.** ADR-001 captures arguments on the producer thread and formats them later on the backend
  thread. The library must make it possible to store arguments by value in the record and format them later.
- **Hot-path cost.** Formatting should be able to write into caller-provided buffers, which matters for the allocation
  policy (ADR-011).

## Decision

**We will use `{fmt}` as the formatting library for the logger, and not `std::format`.**

Details:

1. `{fmt}` is added as an engine runtime dependency with a pinned Git tag, fetched by CMake `FetchContent` as required
   by [foundation ADR-005](../../../foundation/adr/ADR-005-dependency-management-strategy.md). The tag, licence, and
   linkage are recorded in `cmake/Dependencies.cmake` when the implementation lands; this ADR does not invent a version
   ahead of that. The version is bumped deliberately, as an upgrade, not floated.
2. All logger-facing format strings, formatter specialisations and buffers use `fmt::` types. `std::format` is not used
   inside the `logging` module. The APIs this design needs are `fmt::format_string`, `fmt::formatter<T>`,
   `fmt::format_to` / `fmt::format_to_n` (caller-provided buffers, including the ADR-015 stack buffer), and
   `fmt::vformat` / `fmt::runtime` on the backend (ADR-004, ADR-008).
3. Custom type formatting is done with `fmt::formatter<T>` specializations (see ADR-008).
4. **Who includes `{fmt}`.** Public logging headers include `{fmt}` because the call-site API takes
   `fmt::format_string` (ADR-004, ADR-005). Call sites that only log built-in types include those logging headers and
   do **not** write `#include <fmt/...>`. A module that **defines** `fmt::formatter<T>` for an engine type includes
   `{fmt}` next to that type (ADR-008). The backend translation units include `{fmt}` for `vformat` and buffer types.

## Alternatives considered

### `std::format` (C++20/23)

The standard facility has no dependency, no build integration cost and no version to pin. It was not chosen because:

- Its feature set is bound to the language standard version. Features the logger would benefit from are newer than
  C++23, so they would be unavailable, or would have to be reimplemented.
- Completeness and behaviour depend on the standard library implementation. Different toolchains reach feature parity at
  different times, which conflicts with the goal of identical behaviour on every platform.
- Fixes and improvements only arrive with a compiler or standard library upgrade, which the project does not control.

This is the option to revisit once the project moves to a standard where the needed features are universally available.

### `std::format` with a compatibility layer that can fall back to `{fmt}`

An abstraction header would alias to `std::format` where it is sufficient and to `{fmt}` otherwise. It would keep the
door open for dropping the dependency later. It was not chosen because it adds an indirection layer to maintain, it
means two code paths that behave subtly differently (the exact problem this decision tries to avoid), and custom
formatters would have to support both `std::formatter` and `fmt::formatter`.

### `printf`-style formatting (`snprintf`)

It is available everywhere and fast. It was not chosen because it is not type-safe, it has no extensible formatting for
custom types, and format string mistakes become runtime bugs or undefined behaviour. That makes it incompatible with
ADR-004 and ADR-008.

### `iostream` formatting

It is part of the standard library and extensible through `operator<<`. It was not chosen because it is stateful,
comparatively slow, awkward for localisation-neutral output, and makes it hard to capture arguments for deferred
formatting.

## Consequences

### Positives

- Behaviour is identical across compilers, standard libraries and operating systems, because the formatting code is the
  same source everywhere.
- Newer formatting features are available now, without waiting for them to reach the standard or for every standard
  library to implement them.
- The library offers tools that suit the logger's design, such as compile-time `format_string` checks and formatting
  into caller-provided buffers (`format_to` / `format_to_n`), which supports deferred formatting (ADR-001), the
  allocation policy (ADR-011), and the fatal stack buffer (ADR-015). Owned argument storage is the logger's thunk
  protocol (ADR-008), not `fmt::format_args`.
- Bug fixes and improvements come from a library update, independent of compiler upgrades.
- The API is nearly the same as `std::format`, so a later migration to the standard facility would be mostly mechanical.

### Negatives

- It adds a third-party dependency, with the cost of integrating, pinning, updating and auditing it.
- Custom formatters are written against `fmt::formatter<T>`, not `std::formatter<T>`, so they are not reusable with
  `std::format` as they stand.
- Compile times and binary size may be affected by the library's templates. Every translation unit that logs includes
  `{fmt}` transitively through the public logging headers; modules that specialise formatters include it directly.
- Engine code that already uses or later adopts `std::format` elsewhere ends up with two formatting systems in the
  codebase, which needs a clear convention.
- The project depends on the library's maintenance and release cadence.

### Revisit when

The project moves to a standard version whose `std::format` provides everything the logger needs on every supported
toolchain, or the third-party dependency becomes a maintenance burden.
