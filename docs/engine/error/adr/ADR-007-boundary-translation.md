# ADR-007: Boundary translation {#adr_err_007}

| Field        | Value                                                    |
|--------------|----------------------------------------------------------|
| **Status**   | Accepted                                                 |
| **Date**     | 2026-11-24                                               |
| **Deciders** | Cas Rovers                                               |
| **Module**   | `error` (`viscera`)                                      |
| **Related**  | Depends on: ADR-002, ADR-005, ADR-006 · Supersedes: none |

## Context

The engine will call C APIs, Vulkan, the C++ standard library, and `{fmt}`. Those calls fail with `errno`, `VkResult`,
`std::error_code`, or exceptions. If those types appear in a public engine header, every caller learns the foreign
API, and a backend change breaks the engine API.

[ADR-002](ADR-002-domains-codes-and-descriptions.md) says a new code exists only when callers react differently.
[ADR-005](ADR-005-logging-ownership.md) says translators are not
handlers. [ADR-006](ADR-006-panic-assertions-and-exceptions.md)
says `std::bad_alloc` panics and other foreign exceptions are caught at the boundary, not in the middle of the engine.

No `io` or `render` subsystem exists yet. The patterns below are the contract for the first `.cpp` that calls a foreign
API. They are not a claim that those subsystems are in the tree.

## Decision

**The `.cpp` that calls a foreign API translates the foreign failure into an `ErrorEnum` before returning. Public
headers speak only in engine codes. The native name and number live in the context message.**

Details:

1. **Foreign types and codes do not appear in a public engine header.** `errno` constants, `VkResult`, HRESULT values,
   and `std::error_code` are not the engine's public error type. A `.cpp` may use them locally. The header returns
   `Result<T>` and, when the caller must branch, names an engine enum such as `IoError`.
2. **Map to a code the caller can act on.** Many native codes collapse into one engine code. Add an enumerator only
   under the granularity rule in [ADR-002](ADR-002-domains-codes-and-descriptions.md).
3. **The message carries the native detail.** Include the symbolic name and the number, for example
   `"vkQueueSubmit returned VK_ERROR_DEVICE_LOST (-4)"` or `"open '{}' failed: {} (errno {})"`. The description string
   from `errorMessage` stays the stable engine phrase (`"file not found"`). The native text is context, not a new
   enumerator.
4. **An unmapped native code uses that subsystem's catch-all.** The usual names are `Other` or `BackendFailure`. The
   message still holds the native code. Do not send unmapped failures to `CoreError`. Do not add `CoreError::Internal`
   for them ([ADR-002](ADR-002-domains-codes-and-descriptions.md),
   [ADR-006](ADR-006-panic-assertions-and-exceptions.md)).
5. **Translators do not log.** Logging waits for the handler ([ADR-005](ADR-005-logging-ownership.md)).
6. **Prefer the non-throwing standard-library overload.** `std::filesystem` operations that take `std::error_code&`
   are the ones to call. Compare with `ec == std::errc::no_such_file_or_directory`, not by scattering numeric values.
   `{fmt}` exceptions and any other third-party exception are caught in that `.cpp` and translated. `std::bad_alloc`
   is not translated. It panics (ADR-006).
7. **Read `errno` immediately.** Copy it into a local before any other call. A later `std::strerror` or allocator
   activity is allowed to change `errno`. The local is what the message and the mapping use.
8. **One `.cpp` owns each foreign API.** Callers outside that file do not include the foreign headers to interpret a
   failure. Another subsystem that needs to branch includes the owner's error-enum header only (for example
   `asset` includes `io/IoError.hpp` to call `is(IoError::NotFound)`). It does not include the `io` implementation.
9. **A catch-all `catch (...)` at a subsystem boundary becomes that subsystem's catch-all code**, with the fact that
   the type was unknown recorded in the message. It does not become a new `CoreError` enumerator. Thread entry points
   follow ADR-006: if the exception cannot cross the thread boundary as a `Result`, the entry point panics.

Illustrative shape for a future file reader. This is the pattern, not shipped code:

```cpp
Result<std::vector<char>> readFile(const std::string& path)
{
    std::FILE* const file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        const int native = errno;
        const IoError code = native == ENOENT ? IoError::NotFound
                           : native == EACCES ? IoError::PermissionDenied
                                              : IoError::Other;
        return fail(code, "open '{}' failed: {} (errno {})",
            path, std::strerror(native), native);
    }
    // read, close, and translate later failures the same way
}
```

A higher layer wraps only when its callers react to a different abstraction:

```cpp
if (!bytes)
{
    if (bytes.error().is(io::IoError::NotFound))
    {
        return std::unexpected(std::move(bytes).error().wrap(
            AssetError::NotFound, "texture '{}' is not in the asset store", name));
    }
    return std::unexpected(
        std::move(bytes).error().withContext("loading texture '{}'", name));
}
```

`is` sees the top-level code only ([ADR-004](ADR-004-context-cause-and-origin.md)). After the wrap, callers see
`AssetError::NotFound`, and the `io` error remains the cause.

## Alternatives considered

### One engine enumerator per native code

`VkErrorDeviceLost` as its own code would preserve the foreign set. It was not chosen because callers do not react
differently to every native code, public headers would track a backend's enum, and the message already keeps the
native name for diagnostics.

### Let `std::error_code` cross the public API

Filesystem already speaks `std::error_code`. It was not chosen because the public failure type is `Error`
([ADR-001](ADR-001-engine-wide-error-model.md)). The `.cpp` maps `std::errc` values and drops the `error_code` at the
boundary.

### Log the foreign failure at the translation site

The native code is easiest to log where it is in scope. It was not chosen because the message keeps the native code,
and a log at the boundary plus a log at the handler is two lines for one failure
([ADR-005](ADR-005-logging-ownership.md)).

## Consequences

### Positives

- Backends can change native enums without changing a public header.
- Callers branch on a small engine enum. Bug reports still contain `errno` or `VkResult` in the text.
- Asset code can test `IoError::NotFound` without depending on `io`'s `.cpp`.

### Negatives

- Collapsing native codes loses distinctions that no current caller needs and a future caller might. The remedy is a
  new enumerator under ADR-002, not a leak of the native type.
- Translators have to be careful with `errno` and with exceptions. A missed immediate copy, or a catch in the wrong
  function, will show up as `Other` with a stale number.
- The examples name `IoError` and `AssetError` before those headers exist. Implementers add the headers with the
  subsystem, not ahead of it.

### Revisit when

A public API must expose a foreign code because a caller outside the engine has to pass it back to the same foreign
library unchanged. That is a new decision for that subsystem. It does not put the foreign type into `Error`.
