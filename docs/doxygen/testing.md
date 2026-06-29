@page library_testing Correctness testing

Correctness tests answer whether behavior is right. They must not contain benchmark loops, timing thresholds, or performance-regression policy.

## Run tests

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

Run all modules directly:

```powershell
.\build-validation\GameWIPTests.exe --no-manual-ui
```

Run one module:

```powershell
.\build-validation\GameWIPTests.exe `
  --test-module=filesystem `
  --test-report=logs/tests/filesystem_test_report.txt
```

Run the opt-in TestSupport prompt checks:

```powershell
.\build-validation\GameWIPTests.exe --test-support-manual
```

Normal automated runs leave manual UI disabled.

The development preset links the same modules into `GameWIP`, where they run before game startup.

## Reports and exit behavior

Relative `--test-report` paths are resolved beneath `%TEMP%/GameWIP` on Windows, so the example above writes to `%TEMP%/GameWIP/logs/tests/filesystem_test_report.txt`. Absolute paths are honored as explicit overrides. `--no-test-report` disables the file while preserving console results.

Normal validation uses minimal suite output: failures, skips, and manual instructions. The validation runner adds one result line per module and one final aggregate result, avoiding duplicate suite and module summaries. `--verbose-tests` also mirrors passing checks, informational lines, metrics, suite results, summaries, and stress diagnostics to stdout. The report file always receives the complete output.

Each failing expectation emits `[FAIL]` with its suite, reason, source file, line, and function. A suite continues after an expectation failure so one run can report multiple defects. The module returns nonzero when any suite failed; the validation runner returns nonzero when any module failed; CTest and GitHub Actions therefore mark the corresponding module entry as failed. Child-process modes preserve their exact child exit code.

The runner prints the absolute report location and a final `[VALIDATION] result=PASS|FAIL` summary. CTest uses one stable report filename per module and `--output-on-failure` exposes captured console details when a module fails.

The output layers intentionally do not repeat the same success information. A normal successful module contributes one `[VALIDATION] module=...` line; its TestSupport `[RESULT]` and `[SUMMARY]` records remain in the report. Failures, skips, and manual instructions still appear immediately because they require attention. Use `--verbose-tests` when diagnosing ordering or intermediate metrics interactively.

## Artifact lifecycle

Test fixtures, subsystem logs, and benchmark output use `TestSupport::ScopedTemporaryDirectory` beneath the OS temporary directory. Scoped cleanup removes complete workspaces on normal return and exception unwinding. Only final report files remain under `%TEMP%/GameWIP/logs/tests`; validation does not create `logs/` in the repository or build directory. Future game-runtime logging remains independent of validation reporting.

## Module standard

Each module owns a directory under `game/validation/tests` containing:

- an explicit `CMakeLists.txt`;
- its test implementation and local option header;
- a small `module.cpp` registration adapter.

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

The C++ registration name must match the CMake module name. Give the module a stable order and add a child-argument matcher only when it owns a child-process protocol.

## Test requirements

- Tests are deterministic, order-independent, and safe to run repeatedly.
- Default CTest runs do not open UI or wait for input.
- Temporary files use isolated directories and are removed by scoped cleanup.
- Relative report paths resolve under the GameWIP OS-temp root; only final reports persist.
- Global process state is restored before a module returns.
- Child-process modes route to exactly one owning module.
- New behavior and bug fixes receive focused regression coverage when practical.
- Sleep-based synchronization is avoided; bounded timeouts protect unavoidable process and concurrency waits.
- Report failures do not hide console results or change the behavior under test.
- Test hooks are compiled only when tests are built or embedded.

Long-running stress scenarios may remain correctness tests when they verify invariants rather than compare speed. Use @ref project_benchmarking for throughput and latency measurements.
