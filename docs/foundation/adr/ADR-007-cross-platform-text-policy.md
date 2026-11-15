# ADR-007: Cross-platform text policy

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

Contributors may use Windows, Linux, or macOS. Mixed line endings and encoding surprises cause noisy diffs, broken
parsers, and corrupted binary assets if text rules are unclear.

## Decision

### Line endings

1. **Default: LF** for all text tracked by git (`* text=auto eol=lf` in `.gitattributes`).
2. **Exceptions: CRLF** only for Windows launcher scripts: `*.bat`, `*.cmd`, `*.ps1`.
3. **Editor defaults** in `.editorconfig`: `end_of_line = lf`, UTF-8, final newline, trim trailing whitespace; 4-space
   indent for most files, 2 for YAML/JSON; max line length 120 for C/C++ sources.

### Binary assets

- Mark game asset extensions as **binary** in `.gitattributes` (images, models, audio, fonts, archives, compiled
  shaders, etc.) so git does not alter line endings or attempt merges on blob data.

### Source code

- C++ and headers are **UTF-8** text with LF; no UTF-16 source files.
- Cross-platform path handling in code should use standard library facilities and documented conventions (future engine
  ADRs may define virtual paths and pack formats).

## Consequences

**Positive**

- Consistent diffs and fewer “whole file changed” commits on Windows.
- Binary assets stay intact across clones.

**Negative**

- Windows editors must respect `.gitattributes` (or use an EditorConfig-aware IDE) to avoid accidental CRLF in `.cpp`
  files.

**Follow-up**

- If CI is added, optional checks can verify no CRLF in `engine/` and `game/` sources outside the script exceptions.
