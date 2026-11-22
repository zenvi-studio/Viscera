# ADR-003: CMake project layout {#adr_fnd_003}

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

The repository contains a static engine library, a game executable, optional tests, and shared CMake logic. The layout
must be obvious to IDEs (CLion), scriptable via presets, and stable as modules are added.

## Decision

### Top-level project

- Single top-level `project(Viscera …)` in root `CMakeLists.txt`.
  `cmake_minimum_required(VERSION 3.28...4.4.5)` requires **CMake 3.28 or newer** and caps policies at **4.4.5**.
  Newer CMake still configures; it does not enable policies introduced after 4.4.5.
- **`add_subdirectory(engine)`** and **`add_subdirectory(game)`** for product targets.
- **`engine/tests`** added only when `BUILD_TESTING` is on and `PROJECT_IS_TOP_LEVEL` (so embedding Viscera as a
  subproject does not pull tests).

### Shared modules

- Reusable logic under **`cmake/`** (included from the root), not buried inside `engine/`:
    - `CompilerWarnings.cmake` — toolchain checks and `Viscera::Warnings`
    - `Dependencies.cmake` — `FetchContent` and third-party targets

### Outputs and IDE folders

- Runtimes under **`${CMAKE_BINARY_DIR}/bin`**, archives/libs under **`${CMAKE_BINARY_DIR}/lib`**.
- `USE_FOLDERS ON`; fetched third-party targets grouped under the **ThirdParty** IDE folder.

### Presets and developer ergonomics

- **`CMakePresets.json`** configure presets, all **Ninja**, with separate binary directories. A hidden `base` preset
  sets warnings as errors on. Compiler choice lives on the concrete presets
  ([ADR-002](ADR-002-clang-gcc-msvc.md)):
    - `debug` → `cmake-build-debug` (Clang)
    - `release` → `cmake-build-release` (Clang)
    - `debug-gcc` → `cmake-build-debug-gcc`
    - `release-gcc` → `cmake-build-release-gcc`
    - `debug-msvc` → `cmake-build-debug-msvc` (Windows only)
    - `release-msvc` → `cmake-build-release-msvc` (Windows only)
    - `debug-clang-tidy` → `cmake-build-clang-tidy` (inherits `debug`)
    - `debug-cppcheck` → `cmake-build-cppcheck` (inherits `debug`)
    - `debug-analysis` → `cmake-build-analysis` (inherits `debug`; clang-tidy and cppcheck)
- Test presets **`debug`**, **`debug-gcc`**, and **`debug-msvc`** run CTest with output on failure.
  `debug-msvc` is Windows only.
- **`CMAKE_EXPORT_COMPILE_COMMANDS`** enabled at the root for indexing, clang-tidy, and `VisceraCppcheck`.

### Options (root)

- `VISCERA_WARNINGS_AS_ERRORS` — compiler warnings, and analyzer findings, fail the build (see ADR-006). ON in every
  configure preset through the hidden `base` preset.
- `VISCERA_CLANG_TIDY` — optional clang-tidy during build (see ADR-006).
- `VISCERA_CPPCHECK` — optional cppcheck during build, plus the `VisceraCppcheck` target (see ADR-006).
- CTest included when testing is enabled at top level.

## Consequences

**Positive**

- Clear separation: product code in `engine/` and `game/`, policy in `cmake/`, orchestration at root.
- Presets give repeatable configure lines across machines.

**Negative**

- Root `CMakeLists.txt` will grow; resist moving product rules out of `engine/`/`game/` CMakeLists unless shared.

**Follow-up**

- See [`foundation/diagrams/cmake-targets.puml`](../diagrams/cmake-targets.puml) for the target graph.
