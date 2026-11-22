# Viscera documentation {#mainpage}

Design notes and recorded decisions for the Viscera engine and game. The repo root [`README.md`](../README.md) covers
how to clone, build, and run; this tree explains **why** the project is structured and constrained the way it is.

## Layout

Documentation is grouped by **module**, mirroring major code boundaries. Each module can hold ADRs, diagrams, and
guides in one place. Nested paths under `engine/` (and later `game/`) group feature-specific design.

| Path                                 | Scope                                                                             |
|--------------------------------------|-----------------------------------------------------------------------------------|
| [`foundation/`](foundation/)         | Cross-cutting decisions and guides: language, toolchain, CMake, how we write code |
| [`engine/logging/`](engine/logging/) | Engine **logging** subsystem — architecture, API, queues, sinks, shutdown         |
| `engine/` *(other features)*         | Additional runtime features as they gain ADRs (rendering, input, assets, …)       |
| `game/` *(planned)*                  | Gameplay, content, and game-only concerns                                         |
| [`templates/`](templates/)           | Shared authoring templates (e.g. ADR skeleton)                                    |

**Module indexes** (full ADR lists and summaries):

- \subpage foundation_index "Foundation"
- \subpage engine_logging_index "Engine: Logging"

## Architecture decision records (ADRs)

We use [Michael Nygard’s ADR format](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions), extended
for feature modules where useful.

- **Location:** `docs/<module>/adr/`, or nested such as `docs/engine/<feature>/adr/`
- **Naming:** `ADR-NNN-short-title.md` (three-digit index, kebab-case slug)
- **Numbering:** **Per module** — each folder owns its own `ADR-001`, `ADR-002`, … sequence. Foundation decisions stay
  under `foundation/adr/`; logging decisions stay under `engine/logging/adr/`. When adding a decision, use the next free
  number in that folder only.
- **Status values:** Proposed → Accepted → Deprecated / Superseded (link to the replacing ADR)

### Template

Copy [`templates/ADR.md`](templates/ADR.md) when starting a new ADR (metadata table, alternatives, consequences).
Older foundation ADRs use a shorter heading style; new work should follow the template.

### Foundation ADRs (accepted)

Summaries and links: [foundation/README.md](foundation/README.md#adrs). Current set: **ADR-001** through **ADR-007**
(C++23, compilers, CMake layout, engine vs game, dependencies, static analysis, text policy). Next foundation index:
**ADR-008**.

### Engine — logging ADRs

Summaries and links: [engine/logging/README.md](engine/logging/README.md#adrs). Current set: **ADR-001** through
**ADR-016** (accepted; async architecture through testing and benchmarking).

## Guides

Handbooks that are not ADRs — how to write code, rather than a single recorded decision.

| Guide              | File                                                                               |
|--------------------|------------------------------------------------------------------------------------|
| Coding conventions | [foundation/guides/coding-conventions.md](foundation/guides/coding-conventions.md) |

## Diagrams

PlantUML sources live in `docs/<module>/diagrams/`. Foundation diagrams:

- [`foundation/diagrams/cmake-targets.puml`](foundation/diagrams/cmake-targets.puml) — CMake target link graph
- [`foundation/diagrams/module-boundaries.puml`](foundation/diagrams/module-boundaries.puml) — Engine vs game dependency
  rule

Render with a PlantUML plugin in the IDE, or the PlantUML CLI, when reviewing architecture.

## Licence

Project code and docs are under the MIT Licence — see [`LICENCE.txt`](../LICENCE.txt).
