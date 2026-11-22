# ADR-001: Use C++23 {#adr_fnd_001}

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

Viscera is a learning codebase for a small game engine and game. The project will grow in complexity (systems,
resources, possibly concurrency). Picking a modern language revision early avoids rework and lets the codebase use
current library and language facilities as features are added.

The supported compilers are **Clang**, **GCC**, and **MSVC** ([ADR-002](ADR-002-clang-gcc-msvc.md)), each on a recent
release with C++23 support. An explicit standard flag on all targets keeps that baseline the same across the three.

## Decision

1. Use **C++23** as the project language standard (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`).
2. Disable **compiler-specific extensions** (`CMAKE_CXX_EXTENSIONS OFF`).
3. Propagate the standard on the engine library via `cxx_std_23` on `VisceraEngine` so dependents inherit the same
   requirement.

New translation units and libraries must compile as C++23 unless a future ADR documents an exception.

## Consequences

**Positive**

- Access to C++23 features (e.g. `std::expected`, `std::print` where available, deducing `this`, `if consteval`) as the
  engine matures.
- One clear baseline for books, articles, and clang-tidy checks aimed at modern C++.

**Negative**

- Contributors need a **recent Clang, GCC, or MSVC**; older releases may lag behind on C++23.
- Third-party snippets or tutorials written for C++17/20 may need light adaptation.

**Follow-up**

- If a dependency cannot be built as C++23, document the workaround in ADR-005 or a dedicated dependency ADR rather
  than lowering the global standard silently.
