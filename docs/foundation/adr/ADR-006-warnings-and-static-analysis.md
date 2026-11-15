# ADR-006: Warnings and static analysis

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

Engine code benefits from strict compile-time checks and consistent static analysis. Different targets (engine, game,
tests) should share one warning profile so new code does not regress quality.

Each supported compiler’s own diagnostics are the primary gate. **clang-tidy** and **cppcheck** are optional extra
passes, turned on by presets, so day-to-day Debug builds stay fast until someone asks for analysis.

## Decision

### Compiler warnings

1. Define an **INTERFACE** library `ProjectWarnings` with alias **`Viscera::Warnings`** in
   `cmake/CompilerWarnings.cmake`.
2. Apply a warning set that matches the compiler id ([ADR-002](ADR-002-clang-gcc-msvc.md)):
    - **Clang** (including AppleClang): `-Wall`, `-Wextra`, `-Wpedantic`, conversions, casts, format, modern C++ loop
      checks, plus the Clang-only options `-Wunreachable-code`, `-Wcomma`, `-Wloop-analysis`, and `-Wmove`.
    - **GCC**: the same GNU-style set without those four Clang-only options.
    - **MSVC**: `/W4`, `/permissive-`, extra level-4 conversion and override warnings, and `/utf-8`.
3. **clang-cl** and any other compiler id fail configure. Supported ids are Clang, AppleClang, GNU, and MSVC.
4. Link **`Viscera::Warnings` privately** on `VisceraEngine`, `VisceraGame`, and `VisceraEngineTests` so all product
   code shares the flags for the compiler in use.

### Warnings as errors

- One option, **`VISCERA_WARNINGS_AS_ERRORS`**, declared in the root `CMakeLists.txt`. When ON it sets
  `CMAKE_COMPILE_WARNING_AS_ERROR`.
- The same option appends `--warnings-as-errors=*` to the clang-tidy command and `--error-exitcode=1` to the
  per-translation-unit cppcheck command.
- **CMake presets** turn this **ON** through the hidden `base` preset, so every configure preset treats warnings as
  errors, including `debug-clang-tidy`, `debug-cppcheck`, and `debug-analysis`.

### clang-tidy

- Repository **`.clang-tidy`** at the root: wide check enablement with project-specific **naming conventions** and
  pragmatic suppressions for game/engine style. `WarningsAsErrors` is **not** a key in that file.
- **`HeaderFilterRegex`** limits tidy diagnostics in headers to `engine/`, `game/`, and their `include|src|tests`
  `.hpp` paths.
- Option **`VISCERA_CLANG_TIDY`**: when ON with Clang or AppleClang, CMake sets **`CMAKE_CXX_CLANG_TIDY`** for every C++
  compilation. Missing `clang-tidy` prints a warning and configure continues. With GCC or MSVC the option prints a
  warning and leaves `CMAKE_CXX_CLANG_TIDY` unset.
- Preset **`debug-clang-tidy`** turns the option on. Preset **`debug-analysis`** turns it on together with cppcheck.
  Both inherit the Clang `debug` preset.

### cppcheck

- Option **`VISCERA_CPPCHECK`**: when ON, CMake sets **`CMAKE_CXX_CPPCHECK`** (and the same command for C) so each
  translation unit is checked during the build. Checks are `warning,style,performance,portability` at `--std=c++23`,
  with `--inline-suppr` and **`.cppcheck-suppressions`**.
- That suppressions file is the build-time list. It currently holds two global ids, `missingIncludeSystem` and
  `unmatchedSuppression`. The **`.cppcheck`** GUI project mirrors those two ids and does not load a platform library.
- A custom target **`VisceraCppcheck`** (created only when the option is ON and `cppcheck` is found) runs cppcheck on
  `compile_commands.json`, skips `_deps`, and always uses `--error-exitcode=1`. It is not part of the default build.
  Run it with `cmake --build --preset debug-cppcheck --target VisceraCppcheck`.
- Missing `cppcheck` prints a warning and configure continues.
- Preset **`debug-cppcheck`** turns the option on. Preset **`debug-analysis`** turns it on together with clang-tidy.

### Formatting

- **clang-format** via root `.clang-format` (not part of compile, but part of the same quality story).

## Consequences

**Positive**

- One place to tighten or relax warnings for the whole project, including tests.
- clang-tidy naming rules match intended `Viscera::` / `camelBack` style.
- cppcheck can run per file during a preset build, or once over the whole project via `VisceraCppcheck`.

**Negative**

- Strict warnings and either analyzer slow early prototyping. A configure that leaves `VISCERA_WARNINGS_AS_ERRORS` OFF
  still compiles; the presets do not.
- `debug-analysis` runs both tools on every translation unit.

**Follow-up**

- Add a suppression to `.cppcheck-suppressions` only after confirming the diagnostic is a false positive or intentional.
  Keep the same id in `.cppcheck` when it is global.
