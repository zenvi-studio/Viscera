# ADR-002: Clang, GCC, and MSVC {#adr_fnd_002}

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

Supporting every C++ compiler multiplies warning semantics, C++23 conformance gaps, and future CI cost. Viscera keeps a
short allow-list.

Clang, GCC, and MSVC are the toolchains contributors actually have. Each has a C++23 mode strict enough for the standard
chosen in [ADR-001](ADR-001-use-cpp23.md). Warning flags differ across the three, so the project keeps one interface
target and selects the flag list from the compiler id.

Clang is the compiler on the `debug` and `release` presets. It matches clang-tidy and clang-format in
[ADR-006](ADR-006-warnings-and-static-analysis.md). GCC and MSVC are first-class configure paths beside those presets.

## Decision

1. **Supported compilers** are Clang (including AppleClang), GNU (`gcc` / `g++`), and MSVC (`cl`).
   `cmake/CompilerWarnings.cmake` accepts those ids and fails configure for every other id.
2. **clang-cl fails configure.** A Clang id whose frontend variant is `MSVC` is a fatal error. Use `clang++` or `cl`.
3. **Warning flags** stay on the `Viscera::Warnings` interface target, with one list per compiler family
   ([ADR-006](ADR-006-warnings-and-static-analysis.md)):
    - Clang keeps the full GNU-style set, including `-Wunreachable-code`, `-Wcomma`, `-Wloop-analysis`, and `-Wmove`.
    - GCC uses that set without those four Clang-only options.
    - MSVC uses `/W4`, `/permissive-`, extra level-4 conversion and override warnings, and `/utf-8`.
4. **Presets** stay single-config **Ninja**. A hidden `base` preset turns warnings as errors on. `debug` and `release`
   keep `clang` / `clang++` and the existing build directories. `debug-gcc` and `release-gcc` use `gcc` / `g++`.
   `debug-msvc` and `release-msvc` use `cl`, appear only when the host is Windows, and expect a Visual Studio developer
   environment so `cl` is on `PATH`.
5. **clang-tidy** runs only for Clang and AppleClang. The tidy presets inherit `debug`. If `VISCERA_CLANG_TIDY` is ON
   with GCC or MSVC, configure warns and leaves `CMAKE_CXX_CLANG_TIDY` unset. cppcheck stays available on every
   supported compiler.
6. **GoogleTest on MSVC** sets `gtest_force_shared_crt` ON so the test library uses the dynamic CRT
   ([ADR-005](ADR-005-dependency-management-strategy.md)).

Manual configure passes the compiler explicitly, or relies on a preset. See the root README.

## Consequences

**Positive**

- Windows developers can build with the Visual Studio compiler they already have.
- Linux and macOS developers can build with the system GCC as well as Clang.
- `debug` and `release` commands configure Clang.

**Negative**

- Three warning dialects mean a warning-clean build on one compiler can still fail on another.
- MSVC presets require a Visual Studio developer shell, because they call `cl` by name.
- clang-tidy remains a Clang-only gate, so GCC and MSVC builds skip that pass.

**Follow-up**

- When CI is added, record the Clang, GCC, and MSVC versions in the root README.
