# ADR-002: Domains, codes, and descriptions {#adr_err_002}

| Field        | Value                                                                       |
|--------------|-----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                    |
| **Date**     | 2026-11-24                                                                  |
| **Deciders** | Cas Rovers                                                                  |
| **Module**   | `error` (`viscera`)                                                         |
| **Related**  | Depends on: ADR-001, foundation ADR-004, logging ADR-007 · Supersedes: none |

## Context

[ADR-001](ADR-001-engine-wide-error-model.md) says every recoverable failure is one `Error` value. Callers still need a
stable identity they can branch on: "this texture is missing" is a different reaction from "this texture is corrupt".
That identity has to survive after the original enum type has been erased, because a formatter and a caller higher up
the stack only have the `Error`.

Logging categories are a closed bitmask owned by the logger
([logging ADR-007](../../logging/adr/ADR-007-levels-and-category-filtering.md)). Error domains follow the same idea for
a different purpose. A domain says which subsystem owns the code. A logging category says who handled the event. They
are not required to match, and this ADR does not add logging categories.

The engine does not yet have `io`, `platform`, `asset`, `render`, `audio`, or `input` runtime APIs. Empty error headers
for those subsystems would freeze names that no code uses.

## Decision

**A failure's identity is an `ErrorCode`: a closed `ErrorDomain` plus a numeric value. Each subsystem owns the enum
that produces those values and the static description string. `Error` copies that description in when it is built.**

Details:

1. **`ErrorDomain` is a closed `enum class` in `namespace viscera`.** The v1 enumerator is `Core` only. The names
   `Platform`, `Io`, `Asset`, `Render`, `Audio`, `Input`, and `Game` are reserved for later enumerators. They are not
   declared until the change that adds that owner's first error enum. `Game` is reserved the same way. Reserving the
   name does not put a game header into the engine
   ([foundation ADR-004](../../../foundation/adr/ADR-004-engine-vs-game-split.md)).
2. **Adding a domain edits this central header and rebuilds dependents.** That is accepted for the current modular
   monolith. The new enumerator is appended. Existing enumerators are not renumbered, and a removed enumerator's value
   is not reused. The same change updates `domainName()` and amends the list in this ADR.
3. **`domainName(ErrorDomain)` returns a lowercase `std::string_view` with static lifetime** (`"core"`, and later
   `"io"`, `"asset"`, and so on). It is a `constexpr` function in the domain header. The `switch` has no `default`.
   After the switch, an out-of-range value returns `"unknown"`. Warnings-as-errors and `-Wswitch` then fail the build
   when a new enumerator has no arm, without a default arm that would hide it.
4. **`ErrorCode` is the pair `(ErrorDomain domain, std::uint16_t value)`.** It is equality-comparable, `noexcept`, and
   trivially copyable. There is no `Ok` or `None` value. Success is the expected side of `Result`. The contract is the
   two fields, not a particular `sizeof`. The implementation `static_assert`s trivial copyability and standard layout
   on the supported ABIs. Tests record the measured size. They do not treat a guessed size as the contract.
5. **Subsystem codes are `enum class` values owned next to that subsystem's API.** A code opts in by satisfying
   `ErrorEnum`:

    ```cpp
    template <typename E>
    concept ErrorEnum =
        std::is_scoped_enum_v<E>
        && std::unsigned_integral<std::underlying_type_t<E>>
        && sizeof(E) <= sizeof(std::uint16_t)
        && requires (const E code)
        {
            { errorDomain(code) } noexcept -> std::same_as<ErrorDomain>;
            { errorMessage(code) } noexcept -> std::same_as<std::string_view>;
        };
    ```

   `errorDomain` and `errorMessage` live in the enum's namespace so ADL finds them. Both are `constexpr` and
   `noexcept`. `errorMessage` returns a string literal or other static-lifetime storage. The concept checks the
   signature and `noexcept`. Tests `static_assert` the `constexpr` messages.
6. **Values are explicit, non-zero, and append-only.** The first enumerator is `= 1`. A number that has shipped in a
   log line, a bug report, or a test is never reused. Removing an enumerator leaves the number retired in a comment.
7. **Constructing an `Error` from an `ErrorEnum` copies `errorMessage(code)` into the `Error`.** After that, the
   original enum type is gone. Formatters and callers read the stored view. They do not look the description up in a
   central table. There is no `describe(ErrorCode)` switch that lists every subsystem's codes. A new subsystem must
   not need an edit in the central error `.cpp` to give its codes a description.
8. **Add a code only when a caller would react differently.** `NotFound` means "use a placeholder or skip". `Corrupt`
   means "report a content bug". `UnsupportedFormat` means "rebuild the asset". Finer differences go in the context
   message ([ADR-004](ADR-004-context-cause-and-origin.md)), not in a new enumerator.
9. **`CoreError` is for codes whose meaning and caller reaction are shared by more than one subsystem.** The header
   `CoreError.hpp` appears with the first such code. It is not added empty, and it is not a home for `Unknown`,
   `Failed`, `Internal`, or `InvalidState`. A subsystem that must keep an unmapped native failure uses its own
   catch-all, such as `Other` or `BackendFailure` ([ADR-007](ADR-007-boundary-translation.md)).
10. **Until a production enum exists, tests prove `ErrorEnum` with a test-only enum.** That enum is not a stand-in for
    `CoreError` and is not part of the public umbrella.

`errorMessage` uses the same switch shape as `domainName`: one arm per enumerator, no `default`, then a trailing
return of a static `"unknown …"` view for a value that is not an enumerator.

## Alternatives considered

### One central switch that names every code

A single `describe(ErrorCode)` in one `.cpp` can render any code without storing a string view, and `-Wswitch` on the
domain can force every domain to be wired. It was not chosen because every new subsystem would edit the central error
implementation, which fights subsystem ownership. The description is captured from the subsystem's own hook instead.

### Open categories, in the style of `std::error_code`

Virtual category singletons would let a plugin register a domain without editing `ErrorDomain`. It was not chosen for
the same reason as in [ADR-001](ADR-001-engine-wide-error-model.md): the engine is a modular monolith with a closed
set of owners. An open registry is the revisit, not v1.

### Pre-declare every future domain and an empty `XxxError` header

Declaring `Io`, `Asset`, `Render`, and the rest now would make the closed list visible in code immediately. It was not
chosen because those subsystems do not exist, and empty enums become dumping grounds. The reserved names in this ADR
are the allocation of future identifiers.

## Consequences

### Positives

- Callers compare a stable `(domain, value)` pair after the enum type has been erased.
- Descriptions stay next to the enum that owns them.
- Adding a domain is a visible header change, caught by `-Wswitch` when `domainName()` is not updated.
- `Core` can exist before any cross-cutting code does. The domain list is not empty, and `CoreError` is not invented
  to fill it.

### Negatives

- A new domain changes a header that every error-using translation unit includes.
- A description view is only safe because the contract requires static storage. A temporary `std::string` returned from
  `errorMessage` would dangle. Review and the tests in [ADR-008](ADR-008-testing-and-toolchain-baseline.md) have to
  hold that line.
- Numeric stability is a social contract. Nothing in the type system stops someone reusing `= 2`.

### Revisit when

A subsystem outside the engine binary must introduce a domain without an engine header change. Until then, amend this
ADR in the same change that appends an enumerator.
