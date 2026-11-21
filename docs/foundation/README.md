# Foundation

Repository-wide decisions that apply to the whole Viscera codebase: language standard, compiler, build layout,
dependency fetching, quality gates, how text files are stored in git, and how C++ is structured.

Feature-level design (rendering, weapons, AI, etc.) belongs in future `docs/engine/` and `docs/game/` modules, not here.

## Contents

### ADRs

| ID                                                       | Summary                                                                        |
|----------------------------------------------------------|--------------------------------------------------------------------------------|
| [ADR-001](adr/ADR-001-use-cpp23.md)                      | Target **C++23** with standard-required, no compiler extensions                |
| [ADR-002](adr/ADR-002-clang-gcc-msvc.md)                 | **Clang, GCC, and MSVC**; other compilers fail configure                       |
| [ADR-003](adr/ADR-003-cmake-project-layout.md)           | Top-level CMake, presets, output dirs, `cmake/` modules                        |
| [ADR-004](adr/ADR-004-engine-vs-game-split.md)           | Static **engine** library + **game** executable; one-way dependency            |
| [ADR-005](adr/ADR-005-dependency-management-strategy.md) | **FetchContent** for third-party deps; tests gated on top-level build          |
| [ADR-006](adr/ADR-006-warnings-and-static-analysis.md)   | Shared **warnings** interface target; optional **clang-tidy** and **cppcheck** |
| [ADR-007](adr/ADR-007-cross-platform-text-policy.md)     | **LF** text in repo; `.gitattributes` + `.editorconfig`; binary asset rules    |

### Guides

| File                                                  | Summary                                                                           |
|-------------------------------------------------------|-----------------------------------------------------------------------------------|
| [coding-conventions.md](guides/coding-conventions.md) | SOLID, simplicity, modularity, and C++ resource rules, with prefer/avoid examples |

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
