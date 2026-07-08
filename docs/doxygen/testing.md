@page project_testing Correctness testing

Correctness tests answer whether behavior is correct. They must not contain benchmark loops, timing thresholds, or performance-regression policy.

## Scope

This page documents how to run correctness tests, how reports and exit codes behave, how modules should be structured, and what standards new tests must follow.

Runner architecture and command-line ownership are documented in @ref project_validation. Performance measurements are documented in @ref project_benchmarking.

## Common workflow

Configure, build, and run all validation tests through CTest:

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

The same CTest run also verifies reviewed shared-library exports and builds a separate consumer against a clean install prefix. That consumer has no source-tree include paths or short build-tree targets, so package dependency leaks and public-header leaks fail validation.

## Commands

### Run all modules directly

```powershell
.\build\validation\GameWIPTests.exe
```

Use this when you want the validation runner's direct console output instead of CTest grouping.

### Run one module

```powershell
.\build\validation\GameWIPTests.exe `
  --test-module=filesystem `
  --test-report=logs/tests/filesystem_test_report.txt
```

Use focused module runs while debugging one library.

### Disable the retained report

```powershell
.\build\validation\GameWIPTests.exe --test-module=logger --no-test-report
```

Use this for quick local iteration when console output is enough.

### Mirror full suite output to the console

```powershell
.\build\validation\GameWIPTests.exe --test-module=logger --verbose-tests --no-test-report
```

Use this when diagnosing ordering, intermediate metrics, or stress diagnostics interactively.

### Run opt-in manual checks

```powershell
.\build\validation\GameWIPTests.exe --test-module=test_support --manual-ui
.\build\validation\GameWIPTests.exe --test-module=terminal --manual-ui
```

Manual checks are disabled in unattended validation runs.

### Run the Logger fatal-popup check

```powershell
.\build\validation\GameWIPTests.exe --test-module=logger --logger-popup
```

Run this check intentionally and normally only once per validation session.

## Reports and exit behavior

Relative `--test-report` paths resolve beneath `%TEMP%/GameWIP` on Windows. Absolute paths are honored as explicit overrides. `--no-test-report` disables the retained file while preserving console results.

Normal validation uses minimal suite output: failures, skips, and manual instructions. The validation runner adds one result line per module and one final aggregate result. This avoids duplicate suite and module summaries. `--verbose-tests` also mirrors passing checks, informational lines, metrics, suite results, summaries, and stress diagnostics to stdout. The retained report always receives the complete output.

Each failing expectation emits `[FAIL]` with its suite, reason, source file, line, and function. A suite continues after an expectation failure so one run can report multiple defects.

Exit behavior is:

- A module returns nonzero when any suite failed.
- An escaped module exception becomes a failed module result.
- The validation runner returns nonzero when any selected module failed.
- CTest and GitHub Actions mark the corresponding module entry as failed.
- Child-process modes preserve their exact child exit code.

The runner prints the absolute report location and a final `[VALIDATION] result=PASS|FAIL` summary.

## Artifact lifecycle

Test fixtures, subsystem logs, and benchmark output use `TestSupport::ScopedTemporaryDirectory` beneath the operating-system temporary directory. Scoped cleanup removes complete workspaces on normal return and exception unwinding.

Only final report files remain under `%TEMP%/GameWIP/logs/tests` unless a test explicitly retains a diagnostic artifact. Validation must not create `logs/` in the repository or build directory. Future game-runtime logging remains independent of validation reporting.

## Module standard

Each correctness module owns a directory under `game/validation/tests` containing:

```text
game/validation/tests/<module>/
  CMakeLists.txt
  module.cpp
  <module>_test.cpp
```

Register sources and dependencies with:

```cmake
gamewip_add_test_module(
    NAME filesystem
    SOURCES
        filesystem_test.cpp
        module.cpp
    LINK_LIBRARIES
        FileSystem
        TestSupport
)
```

The C++ registration name must match the CMake module name. Give each module a stable order. Add a child-argument matcher only when the module owns a child-process protocol.

The parent validation directory discovers modules automatically. Do not add a central source list for normal test modules.

## Test requirements

Correctness tests must:

- Be deterministic, order-independent, and safe to run repeatedly.
- Avoid benchmark loops and machine-dependent pass/fail thresholds.
- Avoid sleep-based synchronization unless no deterministic signal exists.
- Use bounded timeouts for unavoidable process and concurrency waits.
- Keep default CTest runs unattended.
- Use isolated temporary directories and scoped cleanup.
- Restore global process state before returning.
- Route child-process modes to exactly one owning module.
- Fail explicitly for multiple child-route owners or conflicting selection rules.
- Convert unexpected module exceptions into failed module results.
- Add focused regression coverage for new behavior and bug fixes when practical.
- Ensure report failures do not hide console results or alter the behavior under test.
- Compile test hooks only when tests or startup validation require them.

Long-running stress scenarios may remain correctness tests when they verify invariants rather than compare speed. Use @ref project_benchmarking for throughput and latency measurements.

## Manual and interactive tests

Manual checks must be opt-in. Default validation must not open UI, display fatal popups, wait for keyboard input, or require a real console.

Manual checks should print clear instructions, support skip behavior when appropriate, and record the manual result in the retained report.

## Maintainer notes

When adding tests:

- Start with the public API behavior being guaranteed.
- Use approved internal hooks only when public APIs cannot make the scenario deterministic.
- Reset hook state before and after scenarios that mutate global, backend, process, or singleton state.
- Keep correctness tests separate from benchmarks.
- Record exact focused commands in the pull request.

## Related pages

- @ref project_validation
- @ref project_benchmarking
- @ref project_coverage
- @ref project_extending
