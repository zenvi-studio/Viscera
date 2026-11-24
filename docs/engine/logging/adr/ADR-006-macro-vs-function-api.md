# ADR-006: Macro versus function API {#adr_log_006}

| Field        | Value                                                                      |
|--------------|----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                   |
| **Date**     | 2026-11-18                                                                 |
| **Deciders** | Cas Rovers                                                                 |
| **Module**   | `logging` (`viscera::logging`)                                             |
| **Related**  | Depends on: ADR-001, ADR-004, ADR-005, ADR-007, ADR-015 · Supersedes: none |

## Context

ADR-004 fixes the shape of a logging call: a compile-time-checked format string followed by the arguments. ADR-005
captures the call site with a wrapper type whose converting constructor runs at the caller's expression, so neither the
format check nor the source location needs a macro. This ADR decides whether the call site itself is still a macro.

The two shapes look similar and behave differently:

- A **function** is an ordinary C++ call. It can be namespaced, overloaded, and stepped through in a debugger. Arguments
  are evaluated before the function runs.
- A **macro** can wrap the call in a condition, so a disabled level never evaluates its arguments, and a compile-time
  floor can remove the call from the binary. It can also hide the `FormatString` wrapper. Macros do not respect
  namespaces, they confuse breakpoints, and a mistake in the replacement list is a compile error far from the macro.

ADR-001 requires a disabled message to cost close to nothing *after the call has begun*: the filter runs before capture.
It does not, by itself, say that the argument expressions are skipped. That gap is the real tradeoff. A function API
evaluates every argument even when the level or category is disabled. A macro API can avoid that, at the cost of making
the macro the thing every call site has to get right.

Constraints that shape the choice:

- **The format check and the caller's location must survive every convenience function.** ADR-004 and ADR-005 already
  require that `trace`, `debug`, and the others forward the wrapper, not a plain string.
- **The module is a learning exercise, and it should stay no more complicated than the engine needs** (ADR-001). Macro
  hygiene would be a second API to teach before the logger has any call sites.
- **Helpers must be able to log on someone else's behalf.** ADR-005 already reserves an explicit-location entry point
  for that, which is also the entry point a macro layer would call if one is added later.
- **Disabled-call cost still matters.** If a call site builds a string or calls into the engine just to log it, that
  work happens even when the line is filtered out. The API has to make that cost visible to the caller.

## Decision

**The public logging API is functions. Macros are not required to log, and the module does not ship a macro layer.**

Details:

1. **Call sites use functions in `viscera::logging`.** The severity functions are `trace`, `debug`, `info`, `warning`,
   `error`, and `fatal`. A generic `log` takes the level as its first argument. Names are functions, so they live in the
   namespace and follow the project's naming rules.
2. **Every convenience function takes the ADR-005 wrapper by value and forwards that object.** It does not rebuild a
   wrapper, and it does not extract a `string_view`. The converting constructor therefore runs at the call site the
   programmer wrote, which keeps both the compile-time check and the caller's source location. Illustratively, with the
   category parameter from ADR-007:

   ```cpp
   template <typename... Args>
   void info(Category category,
             FormatString<std::type_identity_t<Args>...> format,
             Args&&... args) noexcept;
   ```

3. **The illustrative signatures in ADR-004 and ADR-005 gain that category parameter.** The wrapper type itself is
   unchanged. `log` is:

   ```cpp
   template <typename... Args>
   void log(Level level,
            Category category,
            FormatString<std::type_identity_t<Args>...> format,
            Args&&... args) noexcept;
   ```

4. **A lower-level function accepts an explicit `std::source_location`.** The location stands before the format string,
   so the argument pack stays last and the compile-time check still works. Engine helpers that log on behalf of another
   call site use this entry point. A future macro layer would use it too, which is the hook ADR-005 left open. When the
   level is `Fatal`, `fatal`, `log(Level::Fatal, ...)`, and this helper all take the direct console path (ADR-015).
5. **Argument expressions are evaluated even when the message is then discarded.** That is ordinary C++. The filter
   inside the function (ADR-007) still runs before any capture or enqueue, so a disabled call does not touch the queue.
   Callers keep arguments cheap: values, not builders and not formatted strings assembled only for the log.
6. **A compile-time floor can still empty a severity function** (ADR-007). The body is compiled out. The argument
   expressions at the call site are not, because emptying a function does not change when its arguments are evaluated.
   Generic `log()` is not emptied that way: its level is a runtime argument, so it always performs a **runtime** compare
   against the compile-time floor and returns before capture when the level is below that floor.
   `log(Level::Debug, ...)`
   therefore cannot capture in a Release build whose floor is `Info`.

## Alternatives considered

### Macros as the only call-site API

`VISCERA_LOG_INFO(category, "frame {}", n)` expands to a filter check and then the explicit-location function. Disabled
levels do not evaluate arguments, and a compile-time floor can remove the expansion. It was not chosen because ADR-005
already captures the location without a macro, and a macro API would hide the real signatures, break namespacing, and
make every call site depend on a replacement list. The wrapper type would become an implementation detail, which is the
revisit case ADR-005 already names.

### Functions for the implementation, macros as the recommended call site

Both exist, and the guides tell people to use the macros. This gets the cheap disabled call and keeps an explicit
function for helpers. It was not chosen because two ways to log will drift: one will forget to forward the wrapper, and
call sites will mix styles. The engine has no measured cost yet that would justify the second surface.

### A lazy argument API

The function takes a callable that produces the arguments, and the logger invokes it only when the line is enabled.
That skips expensive work without a macro. It was not chosen because it makes every call site noisier than the problem
it solves, and it fights the compile-time format check, which needs the argument types at the call.

## Consequences

### Positives

- Call sites are ordinary functions: namespaced, readable in a debugger, and consistent with ADR-004 and ADR-005.
- There is one way to log. The format check and the source location cannot be bypassed by forgetting a macro.
- Helpers have a supported explicit-location entry point, so a later macro layer does not need a new capture mechanism.
- The header surface stays a set of function templates, which is the API the tests in ADR-016 can call directly.

### Negatives

- A disabled call still evaluates its arguments. `debug("{}", expensive())` runs `expensive()` even when debug output
  is filtered out or compiled out of the body.
- Callers who need the skipped evaluation have to write the condition themselves, or wait for a macro layer.
- The `FormatString` parameter is a less obvious type than `const char*`, so compiler errors on a bad call are the
  `{fmt}` errors ADR-004 already accepted.
- Adding the category argument (ADR-007) makes every call slightly longer than a level-only logger.

### Revisit when

Profiles show that argument evaluation at disabled call sites is a real cost, or a compile-time floor needs to remove
those expressions from the binary. The response is a thin macro layer that calls the explicit-location function, not a
redesign of capture.
