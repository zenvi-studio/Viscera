# Changelog

Versions and release tags follow [Versioning](README.md#versioning).

## [0.0.1] - Initial project skeleton

- Engine library (`Viscera::Engine`) and game executable (`VisceraGame`).
- CMake configuration: C++23, Clang, GCC, and MSVC toolchains, Ninja presets, and `compile_commands.json` export.
- Engine version taken from the CMake project version (`0.0.1`).
- GoogleTest v1.18.0 engine tests for top-level builds.
- Development tooling: `.clang-format`, `.clang-tidy`, optional clang-tidy and cppcheck presets, and warnings-as-errors
  in the presets.
- Foundation docs: ADR-001 through ADR-007, plus a coding-conventions guide.
