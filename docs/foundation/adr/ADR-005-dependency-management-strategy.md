# ADR-005: Dependency management strategy

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

The engine will eventually need graphics, audio, and math libraries. Third-party code must be reproducible, legal to
ship, and easy to audit in a learning repo—without committing large vendor trees unless there is a strong reason.

## Decision

### Default mechanism: CMake FetchContent

- Declare dependencies in **`cmake/Dependencies.cmake`** using **`FetchContent_Declare`** /
  **`FetchContent_MakeAvailable`**.
- Pin **immutable versions** (e.g. Git tags such as GoogleTest **v1.18.0**), not floating branches.

### When dependencies are fetched

- **Engine runtime dependencies:** section reserved in `Dependencies.cmake` (empty while the scaffold is minimal).
- **Test-only dependencies:** GoogleTest is fetched only when **`BUILD_TESTING AND PROJECT_IS_TOP_LEVEL`**, with
  `INSTALL_GTEST OFF`.
- While `Dependencies.cmake` configures, `CMAKE_WARN_DEPRECATED` is turned off and then unset, so fetched projects
  do not hide first-party CMake deprecation warnings. On **MSVC**, `gtest_force_shared_crt` is forced ON before
  GoogleTest is fetched so the library uses the dynamic CRT and matches the rest of the build. Other compilers do not
  force it, so GoogleTest keeps its own default.

### Third-party visibility

- New FetchContent targets are grouped under the **ThirdParty** IDE folder via the before/after target property logic in
  root `CMakeLists.txt`.

### What we avoid for now

- Checking copies of large SDKs into `third_party/` without an ADR.
- System-package-only requirements that break Windows/macOS parity unless documented.

### CMake packages and linking

- Prefer **`Viscera::`-style ALIAS** targets for first-party code; link third-party targets by their upstream names
  inside `Dependencies.cmake` and expose thin wrappers only when needed.

## Consequences

**Positive**

- Fresh clones get the same dependency revisions; no manual submodule sync for GTest.
- Keeps the repo small while the engine has few runtime deps.

**Negative**

- Configure requires **network** (and Git) for first fetch.
- Offline or air-gapped builds need local cache or a future vendoring ADR.

**Follow-up**

- When adding a runtime library (e.g. SDL, Vulkan loader), record version, licence, and linkage in a new ADR or extend
  this one with a “Runtime dependencies” subsection.
