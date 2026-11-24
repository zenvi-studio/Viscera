# ADR-004: Engine vs game split {#adr_fnd_004}

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

Viscera aims to separate **reusable engine code** from **game-specific logic** (Brutal-Doom-inspired combat, content,
rules). Without an explicit boundary, gameplay code tends to leak into headers and libraries that should stay
reusable—or the “engine” becomes a monolith tied to one title.

## Decision

### Targets

| Target                              | Type           | Role                                                          |
|-------------------------------------|----------------|---------------------------------------------------------------|
| `VisceraEngine` / `Viscera::Engine` | Static library | Runtime services, public API under `engine/include/viscera/`  |
| `VisceraGame`                       | Executable     | Entry point (`game/src/main.cpp`), links the engine privately |

### Dependency rule

- **Game may depend on engine; engine must not depend on game.**
- Game code lives under `game/`; engine implementation under `engine/src/`, public API under `engine/include/viscera/`.

### Headers and versioning

- Public headers are listed in a **FILE_SET** on `VisceraEngine` for clarity and install-readiness later.
- Version constants are generated from `Version.hpp.in` into the build tree and exposed like other public headers.

### Tests

- Engine tests live in **`engine/tests/`** and link `Viscera::Engine` only.
- Game tests are reserved (`game/tests` is commented in root CMake); when added, they must not create a reverse link
  from
  engine to game.

## Consequences

**Positive**

- Clear place for new subsystems (rendering, audio) in `engine/` with a stable `Viscera::` API.
- Game iteration can change `main` and gameplay without rebuilding engine internals unnecessarily—subject to API churn.

**Negative**

- Requires discipline: no `#include` from `engine/` into `game/` paths, no game types in engine headers without an ADR.

**Follow-up**

- See [`foundation/diagrams/module-boundaries.puml`](../diagrams/module-boundaries.puml).
