# ADR-014: Logger lifetime and shutdown

| Field        | Value                                                                      |
|--------------|----------------------------------------------------------------------------|
| **Status**   | Accepted                                                                   |
| **Date**     | 2026-11-18                                                                 |
| **Deciders** | Cas Rovers                                                                 |
| **Module**   | `logging` (`viscera::logging`)                                             |
| **Related**  | Depends on: ADR-001, ADR-003, ADR-009, ADR-010, ADR-013 · Supersedes: none |

## Context

The logger owns a thread, a ring, and the sinks. Something has to start that thread after the sinks exist, and
something has to stop it without losing the records already accepted. ADR-001 defers that protocol here. ADR-003 defers
the programmer error of logging before initialization or after shutdown here as well.

Implicit lifetime is a bad fit. A Meyers singleton that starts on the first log call runs during static
initialization, from whichever thread happened to log first, and it is destroyed at static teardown in an order the
engine does not control. Other static destructors then log into a logger that is already gone, or the logger is
destroyed first and they log into a joined thread. The engine has a `main`. It can say when logging starts and when it
ends.

Shutdown must work when the ring is full. If "please stop" were a queue record, ADR-010 could drop it. Shutdown should
also bound how long it waits for **records that have not yet started formatting**. An in-flight `Sink::write` or
`flush` is cooperative: `request_stop` does not interrupt it, and `join` waits for it. Detaching a stuck sink is a
later revisit, not this version.

Constraints:

- **One process-wide logger.** Call sites are free functions (ADR-006). They need a target that exists without being
  passed through every system.
- **The backend starts only in `initialize`, and a normal exit drains it only in `shutdown`.** Static destruction is a
  backstop, not the design.
- **Logging calls stay `noexcept` and `void`** (ADR-003). `initialize` is not a logging call. It is allowed to report
  failure.
- **Producers never wait for the backend** except inside `flush` and `shutdown`, which are called from the engine's
  control path, not from the frame loop's log sites.
- **A full ring cannot postpone shutdown** (ADR-009).
- **No thread logs concurrently with `shutdown`.** Destroying the mutex after `join` is only safe if no producer is
  between a "running" check and `lock()`. That is an engine sequencing rule, not a hot-path refcount.

## Decision

**The engine owns one process-wide logger. `initialize` starts the backend as a `std::jthread`, `shutdown` requests it
to drain and then joins it, and both are called explicitly from `main`. No other thread logs during `shutdown`. Logging
while the logger is not running drops the record, counts it, and asserts in debug. Drain and abandon are different
signals.**

Details:

1. **There is one logger in the process.** Free functions forward to it. Storage may be a function-local static, so
   the object has a defined place to be destroyed. What is forbidden is starting the backend on the first log call.
   The first log before `initialize` does not create the thread.

2. **`initialize` is called once, from the main thread, before other threads log.** It builds the ring (ADR-009),
   creates the configured sinks (ADR-013), samples the wall-clock base (ADR-012), and starts the backend as a
   `std::jthread`. `std::jthread` is C++20, so it is available under the project's C++23 baseline. It returns `bool`.
   `false` means the thread did not start. The exception from the standard library, if the thread constructor throws,
   is caught and becomes that `false`. On failure the logger stays stopped, and no half-started thread is left running.
   A second `initialize` without a `shutdown` is a programmer error: debug assert, return `false`, leave the running
   logger alone.

3. **Configuration that is fixed for the run is in the `initialize` argument:** queue capacity, flush timeout, the
   initial sink list, the file path for a file sink, and the source-root prefix used when shortening paths (ADR-005).
   Level, category, frame, and the enabled flag on a sink stay changeable afterwards (ADR-007, ADR-012, ADR-013).

4. **After `initialize` returns, logging calls are thread-safe** in the sense ADR-009 defined: many producers, one
   backend, until `shutdown` begins.

5. **Engine sequencing contract.** `initialize` runs before any other thread logs. Those threads are joined or have
   otherwise stopped logging **before** `shutdown` runs. `shutdown` then destroys the ring and the mutex. A log call
   concurrent with that destroy is a data race; the logger does not take a producer refcount on the hot path to paper
   over it. A debug assert on a racing call is extra diagnostics, not a substitute for the contract.

6. **A log while stopped is a drop, not a start.** Before `initialize`, and after `shutdown` has begun, the call
   increments the not-running counter and returns. In a debug build it also asserts. It does not terminate a release
   build, and it does not throw. This is the programmer-error path ADR-003 reserved. It is what static destructors hit
   if they log after `shutdown`, not a licence to log during `shutdown` from a live worker.

7. **`request_stop` plus notify means drain.** `shutdown` is idempotent, `noexcept`, and called from `main` before
   other teardown. The sequence is:

    - mark the logger as stopping so new calls take the not-running path;
    - `request_stop()` on the `std::jthread`;
    - `notify_all()` on the condition variable (this does not need a ring slot; a `std::stop_callback` on the thread's
      stop token also notifies, ADR-009);
    - the backend **drains**: while not abandon, wait until a slot is available or stop is requested; if stop and the
      ring is empty, leave the loop; otherwise format, destroy, free, write (ADR-009), and emit the overflow summary
      (ADR-010);
    - `flush` each enabled sink;
    - `join` the `jthread`;
    - destroy the sinks and the ring.

   The join happens before sink and ring destruction so a sink is never destroyed while the backend is inside `write`.
   A producer that still runs (in violation of the sequencing contract) checks `stop_requested()` again inside the
   queue critical section and does not publish. A record that was published before the stop became visible under that
   mutex is part of the drain. A second `shutdown` does nothing. `request_stop` is sticky on that `jthread`, so a later
   `initialize` starts a new `jthread` and resets the counters.

8. **Abandon is a separate atomic, set on timeout.** `flush` waits until the ring is empty and the sinks have been
   flushed, or until the timeout. The default timeout is one second, overridable in the `initialize` config.
   `shutdown` uses the same wait. On timeout the logger increments the flush-timeout counter and **sets abandon**.
   Abandon is not `request_stop`: stop means drain remaining records; abandon means stop popping. The backend then
   destroys every still-occupied slot (run the destroy thunk, do not format), counts those records as dropped, flushes
   sinks, and returns. `join` follows.

9. **Timeout bounds unstarted records, not an in-flight write.** A `write` or `flush` the backend has already entered
   can outlive the timeout. `request_stop` and abandon are cooperative; they do not interrupt a blocking sink.
   `join` waits for that call to return. Process exit can still hang on a stuck sink. That is an owned limit of this
   version; detach-and-leak is the revisit.

10. **The destructor calls `shutdown`.** That covers an engine that returns from `main` without an explicit call, and
    it covers static teardown if the storage outlives the call. Destructors of other static objects that log during
    this window hit the not-running path. The engine should not rely on that window. The supported order is join other
    logging threads, `shutdown` in `main`, then the rest of teardown. If `shutdown` did not run, the `std::jthread`
    destructor still joins. That join is the backstop, not the design: it can run during static teardown, and it blocks
    until the thread function returns. Destroying the `jthread` from inside the backend thread is `std::terminate`, so
    the backend never destroys the logger.

11. **`initialize`, `shutdown`, and `flush` do not run on the hot path and may allocate and join.** They do not throw.
    `initialize` catches a failure to start the thread and returns `false`. `shutdown` and `flush` are `noexcept`
    because they can run while the process is already failing.

12. **Why `std::jthread` rather than `std::thread`.** The destructor joins, so a forgotten `join` cannot
    `std::terminate` the way a still-joinable `std::thread` does. The stop token feeds the `std::stop_callback` that
    notifies the condition variable (ADR-009), which is the full-ring shutdown rule: no queue slot. There is no extra
    dependency, and the thread's lifetime is tied to the object that owns it, which matches the project's RAII rule.
    The cost is that destruction blocks until the thread returns. A stop cannot be cleared, so each session is a new
    `jthread`.

## Alternatives considered

### Start on first use, destroy at static teardown

No `initialize`, and the first log pays for startup. It was not chosen because the first log may be on a worker, the
sink set is then implicit, and destruction order against other statics is undefined from the engine's point of view.
Lost lines during static teardown would be indistinguishable from a logger that had never been shut down cleanly.

### Pass a `Logger&` through every system

There is no process-wide object, and tests construct a logger on the stack. It was not chosen because ADR-006's free
functions have to reach a logger, and threading a reference through every engine call is a tax on code that is not
about logging. Tests use the process-wide instance with a fixture that shuts it down (ADR-016).

### `shutdown` posts a control record and waits forever

The backend stops only when it pops the sentinel, and `flush` has no timeout. It was not chosen because ADR-010 can
drop the sentinel when the ring is full, and because a single stuck `flush` would hang process exit.
`request_stop` is visible with the ring full. The timeout bounds the wait for **unstarted** records.

### A producer refcount so `shutdown` can race with log calls

Each log call would bump a counter around the queue mutex, and `shutdown` would wait for zero before destroying the
ring. It was not chosen because it is extra shared state on every enabled call, for a window the engine can close by
joining workers first. The sequencing contract is the v1 rule. A refcount is the revisit if tests or plugins cannot
honour it.

### `std::thread` plus a separate stop flag

The backend would be a `std::thread`, `shutdown` would store an atomic flag and `join` explicitly, and the destructor
would `join` only if the caller remembered to write that. It was not chosen because a `std::thread` that is still
joinable in its destructor calls `std::terminate`, which is the failure `std::jthread` exists to close, and because
the flag would duplicate `stop_token`. `shutdown` still joins explicitly, before sinks are destroyed. The `jthread`
destructor is what remains when that call was skipped, not a second shutdown policy.

### Abort on a log before `initialize`

The mistake is loud in every build. It was not chosen because a log from a static constructor would then kill the
process before `main`, and because ADR-003's rule is that the logger does not take the engine down. The debug assert
is the loud version. The release behaviour is a counted drop.

## Consequences

### Positives

- Startup order is visible: sinks and the ring exist before any thread is asked to log, and the backend is running
  before `initialize` returns.
- A normal exit drains the records that were accepted, then flushes, then joins. That is the delivery guarantee
  ADR-001 asked for on the non-crash path, within the timeout for records not yet started.
- A full ring cannot swallow the stop signal. `request_stop` and the stop callback's `notify_all` do not need a slot.
- Drain and abandon are different: stop empties the ring; timeout destroys what was not started.
- The backend thread cannot be left joinable by accident. Its destructor joins, and `shutdown` joins before the sinks
  and the ring go away.
- Logging too early or too late is observable: an assert in debug, a counter in every build, and no second lifetime
  hiding inside the first call.

### Negatives

- Every program, and every test, must call `initialize` and `shutdown`. Forgetting `initialize` looks like a logger
  that drops everything.
- The engine must join (or otherwise silence) logging threads before `shutdown`. A worker that logs during destroy is
  a data race, not a counted drop.
- One process-wide instance means tests share it. They have to reset it in a fixture and they cannot run against it in
  parallel (ADR-016).
- The destructor backstop can still run during static teardown. Lines from other static destructors are dropped, which
  is easy to misread as "the engine did not log".
- `flush` and `shutdown` block the caller up to the timeout, and `join` can block longer if a sink is already in
  `write`. Calling them from the frame loop would hitch. They belong on the control path.
- On timeout, queued records that were not started are abandoned. The shutdown delivery guarantee is "within the
  timeout for unstarted records", not "however long the disk takes" and not "a stuck `write` is interrupted".
- `request_stop` cannot be undone. Restarting the logger allocates a new `jthread` instead of clearing a flag.
- The destructor join can run at static teardown and block there. That is safer than `std::terminate`, and it is still
  the wrong place to stop the logger.

### Revisit when

Tests need two live loggers, or a stuck sink should be detached and leaked rather than waited on at `join`, or
producers cannot be silenced before `shutdown` and a hot-path refcount becomes necessary. A second logger is an
explicit object the free functions do not use. It is not a thread-local logger.
