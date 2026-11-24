@page foundation_index Foundation

# Foundation

Repository-wide decisions that apply to the whole Viscera codebase: language standard, compiler, build layout,
dependency fetching, quality gates, how text files are stored in git, and how C++ is structured.

Feature-level design (rendering, weapons, AI, etc.) belongs in future `docs/engine/` and `docs/game/` modules, not here.

## Contents

### Guides

- \subpage foundation_conventions — SOLID, simplicity, modularity, and C++ resource rules, with prefer/avoid examples

### Architecture decisions

- \subpage adr_fnd_001 — Target **C++23** with standard-required, no compiler extensions
- \subpage adr_fnd_002 — **Clang, GCC, and MSVC**; other compilers fail configure
- \subpage adr_fnd_003 — Top-level CMake, presets, output dirs, `cmake/` modules
- \subpage adr_fnd_004 — Static **engine** library + **game** executable; one-way dependency
- \subpage adr_fnd_005 — **FetchContent** for third-party deps; tests gated on top-level build
- \subpage adr_fnd_006 — Shared **warnings** interface target; optional **clang-tidy** and **cppcheck**
- \subpage adr_fnd_007 — **LF** text in repo; `.gitattributes` + `.editorconfig`; binary asset rules

### Diagrams

| File                                                      | Description                                                                                        |
|-----------------------------------------------------------|----------------------------------------------------------------------------------------------------|
| [cmake-targets.puml](diagrams/cmake-targets.puml)         | `VisceraEngine`, `VisceraGame`, tests, `Viscera::Warnings`; tidy and cppcheck are not link targets |
| [module-boundaries.puml](diagrams/module-boundaries.puml) | Source tree boundaries and allowed link direction                                                  |

## Related code

| Concern                                                        | Location                                                                                                                                |
|----------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| C++ standard / project options                                 | Root [`CMakeLists.txt`](../../CMakeLists.txt)                                                                                           |
| Presets (Clang, GCC, MSVC, warnings-as-errors, tidy, cppcheck) | [`CMakePresets.json`](../../CMakePresets.json)                                                                                          |
| Warnings target and analyzer hooks                             | [`cmake/CompilerWarnings.cmake`](../../cmake/CompilerWarnings.cmake)                                                                    |
| Third-party fetch                                              | [`cmake/Dependencies.cmake`](../../cmake/Dependencies.cmake)                                                                            |
| clang-tidy config                                              | [`.clang-tidy`](../../.clang-tidy)                                                                                                      |
| cppcheck GUI project and suppressions                          | [`.cppcheck`](../../.cppcheck) (GUI only; CMake uses `compile_commands.json`), [`.cppcheck-suppressions`](../../.cppcheck-suppressions) |
| Line endings                                                   | [`.gitattributes`](../../.gitattributes), [`.editorconfig`](../../.editorconfig)                                                        |

When you change any of the above in a meaningful way, update the matching ADR or add a new one (next index:
**ADR-008**). The [coding conventions](guides/coding-conventions.md) guide is not an ADR; edit it in place when the
principles change.
