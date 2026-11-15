# Contributing

How to configure, build, test, format, and run static analysis on Viscera. Presets, CMake options, and how to run the
game are in the [README](README.md). Warning and analyzer policy is
[ADR-006](docs/foundation/adr/ADR-006-warnings-and-static-analysis.md).

The commands below are the same in PowerShell.

## Development workflow

From the repository root, with CMake 3.28 or newer, Ninja, and Clang, GCC, or MSVC on `PATH`:

1. **Configure** a Debug preset (Ninja, warnings as errors). `debug` uses Clang. `debug-gcc` uses g++. `debug-msvc`
   uses `cl` and is listed only on Windows, from a Visual Studio developer shell:

   ```bash
   cmake --preset debug
   cmake --preset debug-gcc
   cmake --preset debug-msvc
   ```

2. **Build** with the same preset name:

   ```bash
   cmake --build --preset debug
   ```

3. **Test.** CTest presets `debug`, `debug-gcc`, and `debug-msvc` run `VisceraEngineTests`. `debug-msvc` is Windows
   only:

   ```bash
   ctest --preset debug
   ```

4. **Format** the sources you changed. See [Code formatting](#code-formatting).

5. **Run clang-tidy** with the `debug-clang-tidy` preset. See [Static analysis](#static-analysis). cppcheck presets and
   the `VisceraCppcheck` target are in the [README command table](README.md#common-commands).

## Code formatting

[`.clang-format`](.clang-format) at the repository root defines the formatting rules. `clang-format` finds that file by
walking up from the path you pass.

Format one file in place:

```bash
clang-format -i engine/src/Engine.cpp
```

The same `-i` flag accepts several files:

```bash
clang-format -i engine/src/Engine.cpp engine/include/viscera/Engine.hpp game/src/main.cpp engine/tests/src/EngineTests.cpp
```

## Static analysis

`debug-clang-tidy` is a CMake configure and build preset. It inherits `debug` (Ninja, Clang, warnings as errors) and
sets `VISCERA_CLANG_TIDY=ON`. Its build directory is `cmake-build-clang-tidy/`. These tidy presets stay on Clang.
Enabling `VISCERA_CLANG_TIDY` for GCC or MSVC prints a warning and skips the tool.

```bash
cmake --preset debug-clang-tidy
cmake --build --preset debug-clang-tidy
```

This path is already wired. When `clang-tidy` is on `PATH`,
[cmake/CompilerWarnings.cmake](cmake/CompilerWarnings.cmake) sets `CMAKE_CXX_CLANG_TIDY`. CMake then runs clang-tidy on
each C++ translation unit and passes that unit's compile command. Because the preset turns warnings as errors on, the
command also includes `--warnings-as-errors=*`. Check selection and naming rules come from
[`.clang-tidy`](.clang-tidy) at the repository root.

If the `clang-tidy` executable is missing, configure prints a warning and the build continues without analysis.

There is no separate clang-tidy custom target that walks `compile_commands.json`.
[ADR-006](docs/foundation/adr/ADR-006-warnings-and-static-analysis.md) does not list one as follow-up work. The
whole-project database run that does exist is `VisceraCppcheck`, documented in the README.

A manual clang-tidy run reads the compilation database with `-p`:

```bash
clang-tidy -p cmake-build-debug engine/src/Engine.cpp
```

That invocation is separate from the preset. The preset supplies the compile command itself and does not pass `-p`.

## Compilation database

The root [CMakeLists.txt](CMakeLists.txt) sets `CMAKE_EXPORT_COMPILE_COMMANDS` to `ON`. CMake writes
`compile_commands.json` into the build directory during configuration. For the `debug` preset the file is
`cmake-build-debug/compile_commands.json`. Every other preset writes its own copy
under that preset's binary directory, for example `cmake-build-clang-tidy/compile_commands.json`.

The file records the compiler invocation for each translation unit. IDEs use it for indexing. Standalone clang-tidy
uses it when given `-p`. `VisceraCppcheck` passes it to cppcheck as `--project=`.

## Coding style

Formatting rules live in [`.clang-format`](.clang-format). Static-analysis rules live in [`.clang-tidy`](.clang-tidy).
Those two files are the authority for naming, braces, and checks. This guide does not restate individual checks or
naming rules.

How to structure types, modules, and resources (SOLID, KISS, RAII, and related principles) is in
[coding conventions](docs/foundation/guides/coding-conventions.md).

First-party C++ headers use `.hpp`. See [Project Conventions](README.md#project-conventions).

## Versioning

How the CMake project version becomes `kEngineVersion`, and when to tag a release, is in the
[README](README.md#versioning). Released changes are recorded in [CHANGELOG.md](CHANGELOG.md).
