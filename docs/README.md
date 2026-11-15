# Viscera documentation

Design notes and recorded decisions for the Viscera engine and game. The repo root [`README.md`](../README.md) covers
how to clone, build, and run; this tree explains **why** the project is structured and constrained the way it is.

## Layout

Documentation is grouped by **module**, mirroring major code boundaries. Each module can hold ADRs, diagrams, and
guides in one place.

| Path                         | Scope                                                                             |
|------------------------------|-----------------------------------------------------------------------------------|
| [`foundation/`](foundation/) | Cross-cutting decisions and guides: language, toolchain, CMake, how we write code |
| `engine/`                    | *(planned)* Runtime engine features — rendering, input, assets, etc.              |
| `game/`                      | *(planned)* Gameplay, content, and game-only concerns                             |

Only **foundation** exists today. Add `engine/` and `game/` under `docs/` when the first feature-specific ADRs appear.

## Architecture decision records (ADRs)

We use [Michael Nygard’s ADR format](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions).

- **Location:** `docs/<module>/adr/`
- **Naming:** `ADR-NNN-short-title.md` (three-digit index, kebab-case slug)
- **Numbering:** Global sequence across the repo. Foundation owns **ADR-001–ADR-007**; the next new decision is
  **ADR-008** (whichever module it belongs to).
- **Status values:** Proposed → Accepted → Deprecated / Superseded (link to the replacing ADR)

### Template

```markdown
# ADR-NNN: Title

- **Status:** Accepted
- **Date:** YYYY-MM-DD

## Context

What problem or constraint motivates a decision?

## Decision

What we chose.

## Consequences

Positive, negative, and follow-up work.
```

### Foundation ADRs (accepted)

| ID      | Title                              | File                                                                                                  |
|---------|------------------------------------|-------------------------------------------------------------------------------------------------------|
| ADR-001 | Use C++23                          | [ADR-001-use-cpp23.md](foundation/adr/ADR-001-use-cpp23.md)                                           |
| ADR-002 | Clang, GCC, and MSVC               | [ADR-002-clang-gcc-msvc.md](foundation/adr/ADR-002-clang-gcc-msvc.md)                                 |
| ADR-003 | CMake project layout               | [ADR-003-cmake-project-layout.md](foundation/adr/ADR-003-cmake-project-layout.md)                     |
| ADR-004 | Engine vs game split               | [ADR-004-engine-vs-game-split.md](foundation/adr/ADR-004-engine-vs-game-split.md)                     |
| ADR-005 | Dependency management strategy     | [ADR-005-dependency-management-strategy.md](foundation/adr/ADR-005-dependency-management-strategy.md) |
| ADR-006 | Warnings, clang-tidy, and cppcheck | [ADR-006-warnings-and-static-analysis.md](foundation/adr/ADR-006-warnings-and-static-analysis.md)     |
| ADR-007 | Cross-platform text policy         | [ADR-007-cross-platform-text-policy.md](foundation/adr/ADR-007-cross-platform-text-policy.md)         |

## Guides

Handbooks that are not ADRs — how to write code, rather than a single recorded decision.

| Guide               | File                                                                                       |
|---------------------|--------------------------------------------------------------------------------------------|
| Coding conventions  | [foundation/guides/coding-conventions.md](foundation/guides/coding-conventions.md)         |

## Diagrams

PlantUML sources live in `docs/<module>/diagrams/`. Foundation diagrams:

- [`foundation/diagrams/cmake-targets.puml`](foundation/diagrams/cmake-targets.puml) — CMake target link graph
- [`foundation/diagrams/module-boundaries.puml`](foundation/diagrams/module-boundaries.puml) — Engine vs game dependency
  rule

Render with a PlantUML plugin in the IDE, or the PlantUML CLI, when reviewing architecture.

## Licence

Project code and docs are under the MIT Licence — see [`LICENCE.txt`](../LICENCE.txt).
