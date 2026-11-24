# ADR-016: Testing and benchmarking {#adr_log_016}

| Field        | Value                                                  |
|--------------|--------------------------------------------------------|
| **Status**   | Accepted                                               |
| **Date**     | 2026-11-18                                             |
| **Deciders** | Cas Rovers                                             |
| **Module**   | `logging` (`viscera::logging`)                         |
| **Related**  | Depends on: ADR-001 through ADR-015 · Supersedes: none |

## Context

The logger's guarantees are easy to break with a small change: a convenience function that rebuilds the format wrapper
and reports its own line, a sink write that runs under the queue mutex, a drop that is not counted, a shutdown that
returns while records are still queued. Several earlier ADRs already point at tests for those guarantees. This ADR
collects them and decides how they are run.

The project already fetches GoogleTest when tests are enabled, and engine tests live under `engine/tests/` and link
only the engine (foundation ADR-004 and ADR-005). The logger tests belong there. They should not add a dependency.

Benchmarks are a different kind of check. A disabled call and an enqueue are the costs ADR-001 and ADR-006 claim are
small. A number that fluctuates on a shared CI machine should not fail the build. A number that was never measured
will not trigger the revisit clauses in ADR-006 and ADR-009 either.

Allocation-failure injection is called out in ADR-003 and ADR-011. The producer path is specified not to allocate
(ADR-011), so there is not yet a heap call to inject into. Pretending otherwise would invent an allocator seam for a
test.

Constraints:

- **Tests use the public API** (ADR-006) and the process-wide logger (ADR-014). They do not reach into the ring.
- **Tests need a sink that stores lines in memory**, because asserting on stderr or on a file is brittle and slow.
- **The process-wide logger is shared.** Two tests running at once against it would interleave lines and counters.
- **Compile-time checks cannot be asserted inside a passing test binary.** A mismatch that fails the build never
  reaches `EXPECT`.
- **Timing is not a correctness signal.** Functional tests use counters, line text, and order.

## Decision

**Logger behaviour is tested with GoogleTest and an in-memory sink against the process-wide logger, one test at a
time. Benchmarks are a separate opt-in target with a small in-tree harness, they are not a CI gate, and they do not
add a dependency.**

Details:

1. **The test binary uses the GoogleTest already fetched for engine tests.** There is no second framework. The memory
   sink implements ADR-013 and appends each `write(Level, std::string_view)` line to a `std::vector<std::string>` it
   owns. It records `flush` calls. Tests pass it in the `initialize` config and do not open a file sink. Lines stored
   there contain no color escape codes.
2. **A fixture calls `shutdown` in teardown**, then the next test calls `initialize` with a small ring capacity and a
   fresh memory sink. `initialize` resets counters. Tests are not written to run in parallel against the process-wide
   logger. The fixture is what makes the shared instance safe under the default GoogleTest execution.
3. **Functional coverage, each of these as tests:**
    - the reported file and line for `log` and for every convenience function is the call site, not the forwarding
      function (ADR-005, ADR-006);
    - a level below the minimum, and a category whose bit is clear, produce no record and no sink write (ADR-007);
    - a per-category level override affects only that category;
    - a compile-time floor of `Info` leaves the `trace` and `debug` bodies empty of capture. The test build that covers
      the other cases compiles the floor at `Trace` so those calls still exist. A separate build of the same suite, or a
      focused target, compiles with the floor at `Info` and asserts that a debug call does not increment the enqueue
      path. Argument expressions are not claimed to be skipped (ADR-006);
    - two threads each logging a sequence see that sequence intact inside the combined output (ADR-009);
    - a ring of a few slots, filled past capacity, drops the new records, increments the overflow counter, keeps the
      records already queued, and causes the backend to write one summary line whose count matches the drops (ADR-010);
    - a call whose arguments exceed 256 bytes increments the argument-budget counter and writes nothing (ADR-011);
    - a `const char*` with no NUL in the remaining budget increments the argument-budget counter and is not read past
      that window (ADR-008);
    - a string argument is still readable after the caller's buffer has been overwritten, and a logged pointer is not
      dereferenced (ADR-008);
    - a user-defined argument with a nothrow destructor is destroyed once, after the backend has formatted, and a later
      producer can reuse that slot (ADR-008, ADR-009);
    - `log(Level::Debug, ...)` in a floor-`Info` build does not capture (ADR-006, ADR-007);
    - `clearCategoryMinLevel` makes that category follow a later global `setMinLevel` (ADR-007);
    - a custom formatter that throws becomes one diagnostic record, the destroy thunk still runs, and later records
      still arrive (ADR-003, ADR-008);
    - `fatal`, `log(Level::Fatal, ...)`, and the explicit-location helper with `Fatal` each attempt the direct console
      write (ADR-015);
    - `initialize` failure is not required to be simulated beyond a second `initialize` while running, which returns
      `false` and leaves the first session in place (ADR-014);
    - a call before `initialize`, and a call after `shutdown`, increments the not-running counter and writes nothing;
    - `shutdown` does not return while an accepted record is still unflushed, when the sink is well behaved;
    - `setFrame` is the frame number on a later line from the same thread (ADR-012);
    - the color wrapper, given a terminal, emits the SGR prefix for that level, then the whole unchanged line, then the
      reset, for every level. Given a non-terminal it returns the plain line and no escape codes (ADR-013). This is a
      test of the wrapper, not of a live console; it may use an `isatty` seam.
4. **The format-string check is a compile-fail test, outside the normal test binary.** A small source file contains a
   call whose format string does not match its arguments. A CMake test configures that file and expects **compilation
   to fail** on Clang, GCC, and MSVC. The test does not grep compiler diagnostics: `{fmt}` error text is not stable
   across those toolchains. The CMake check must still compile a control file that logs correctly, so a misconfigured
   "expected failure" cannot pass by failing to compile anything. The normal suite does not include the mismatch file.
   A convenience function that takes a plain string would not fail this test, so the mismatch file calls each
   convenience function, not only `log`.
5. **Counters are read through the snapshot on the logger.** Tests do not inspect atomics directly. The snapshot
   includes the overflow count (ADR-010), the argument-budget count (ADR-011), the not-running count (ADR-014), the
   formatting-failure count (ADR-003), the per-sink write-failure counts (ADR-013), and the flush-timeout count
   (ADR-014).
6. **Benchmarks are an opt-in CMake target, off by default, not part of the test run and not a CI failure.** The
   harness is a few timed loops in engine code. It does not fetch Google Benchmark or any other library. It prints the
   cost of a disabled call and the cost of an enabled enqueue that uses the memory sink. There is no pass/fail
   threshold. When a revisit clause in ADR-006 or ADR-009 asks for a profile, this target is the first measurement.
7. **Allocation-failure injection is not part of this suite.** ADR-011's producer path has no allocation to fail. A
   test that the steady-state enqueue does not call the allocator can be added when there is a seam to observe
   allocations, and is not a reason to add a custom allocator now. The oversized-argument test is the allocation-policy
   test the suite does have: the call fails by not fitting, without touching the heap.

## Alternatives considered

### Assert on a log file or on captured stderr

No memory sink, and the tests read the same destination the engine uses. It was not chosen because file I/O makes the
suite slow and order-dependent, and because stderr mixes with anything else the test process prints. The memory sink
is a normal `Sink`, so the tests also exercise ADR-013.

### A logger object constructed per test, with no process-wide instance

Tests would not need a fixture protocol, and they could run in parallel. It was not chosen because it would test a
different entry point from the one call sites use. The free functions and the lifetime in ADR-014 are part of the
behaviour under test. The fixture is the cost of that.

### Google Benchmark, or thresholds that fail CI

A known tool, and a regression would go red. It was not chosen because it is a new dependency for a number the module
does not need to gate, and because a threshold on enqueue latency will fail on a loaded machine without saying the
logger is wrong. The opt-in harness keeps the measurement next to the code until a number is actually needed.

### Inject allocation failure before the producer is written

Override `operator new` in the test binary and expect the logger to swallow it. It was not chosen because the hot path
is specified not to allocate, so a failure injected into the general allocator fires on the test framework and the
sinks, not on a logger path that can be asserted. The injection ADR-003 mentions waits until a change introduces a
real allocating capture. ADR-008's revisit, the heap fallback, is that change.

## Consequences

### Positives

- The behavioural guarantees from ADR-003 through ADR-015 have a listed test, written against the API the engine will
  call.
- The memory sink and the configurable ring size make overflow and capture tests fast and deterministic.
- The compile-fail test locks the ADR-004 guarantee, including the convenience functions that could silently drop it.
- Benchmarks exist without a dependency and without a flaky CI check.
- The suite does not invent an allocator seam the implementation does not need.

### Negatives

- Tests share one process-wide logger. A test that forgets the fixture, or a future parallel test runner, will flake in
  ways that look like logger bugs.
- The source-location test is coupled to line numbers in the test file. Reformatting that file can break it. The usual
  defence is to capture the expected line immediately above the call and keep that pair stable.
- The compile-fail CMake test is a different mechanism from GoogleTest, and a misconfigured "expected failure" can pass
  the suite by not compiling the file at all. A companion file that must compile, plus a non-zero status on the
  mismatch file, is the portable check. Matching `{fmt}` diagnostic text across Clang, GCC, and MSVC is not required.
- Two builds are needed to claim both "every level works" and "the release floor compiles `debug` out". One test
  binary cannot do both.
- Benchmark numbers are not tracked. A slowdown can land and stay until someone runs the opt-in target.
- There is no test yet that a steady-state enqueue avoids the allocator. The argument-budget test is a stand-in, not a
  proof.

### Revisit when

The producer grows an allocating fallback, the tests start needing two loggers or parallel execution, or a benchmark
number becomes a regression the project is willing to gate. Allocation injection and a per-test logger object belong
to those changes, not to the first suite.
