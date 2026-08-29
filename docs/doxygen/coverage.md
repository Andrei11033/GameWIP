@page project_coverage Coverage workflow

Coverage is an opt-in validation workflow that reports which correctness-test paths executed.

The sections below explain the coverage preset, exactly which code contributes
to the report, where the HTML and machine-readable artifacts go, and how to
diagnose an incomplete or failed run.

## Common workflow

Run coverage from the repository root:

```powershell
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
cmake --build build/coverage --target coverage
```

The coverage workflow requires standalone tests.

## Build controls

`GAMEWIP_ENABLE_COVERAGE=ON` adds GCC/Clang coverage instrumentation and creates the `coverage` target. The `coverage` preset enables this option and
disables the game executable.

The project rejects `GAMEWIP_ENABLE_COVERAGE=ON` when `GAMEWIP_BUILD_TESTS=OFF`.

## Outputs and artifacts

The `coverage` target uses `gcovr` to write:

```text
build/coverage/coverage/index.html
build/coverage/coverage/coverage.xml
```

The report includes maintained implementation sources for Base, Unicode, IO, FileSystem, Terminal, Logger, Assert, TestSupport, and Desktop, plus the
modular correctness-test sources under `game/validation/tests`. Header-only Base code contributes where instantiated by tests and consumers.
Third-party sources under `external/`, generated build output, and intentionally provisional engine code are excluded.
Coverage data discovery is restricted to the active coverage build tree so archived or neighboring instrumented builds cannot contaminate the report.

GCC profile updates are atomic so parallel test processes cannot overwrite one another's counters. Corrupt or negative profile data is a report
failure. The workflow must not suppress parser errors. Identical inline or template functions can be emitted with different declaration line records
across translation units, so reports merge those records at the lowest reported line while preserving their combined coverage.

## CI behavior

The Validation workflow runs the coverage preset on Windows and uploads the HTML/XML output as a build artifact.

Coverage percentages are informational rather than validation gates. A lower
coverage value does not fail validation; the workflow fails only when its
configuration, tests, or report generation fail.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Configure fails. | Coverage was enabled without standalone tests. | Use the `coverage` preset or enable `GAMEWIP_BUILD_TESTS`. |
| The `coverage` target is missing. | The project was not configured with coverage instrumentation. | Reconfigure with `cmake --preset coverage`. |
| The report contains corrupt or negative profile data. | Test processes produced invalid coverage counters. | Treat the report as failed and investigate the affected test or toolchain behavior. |
| Report generation rejects differing line records for the same inline or template function. | Translation units emitted equivalent function records at different declaration lines. | Keep `merge-use-line-min` enabled so gcovr merges the equivalent records deterministically. |
| Third-party files appear in the report. | The exclusion list is incomplete. | Update the coverage helper to exclude vendor and generated paths. |

## Maintainer notes

When changing coverage behavior:

- Keep coverage separate from normal validation.
- Keep third-party and generated sources excluded.
- Update the documented inclusion list and report filters together when coverage scope changes.
- Do not replace focused tests with a percentage target.
- Preserve report failure visibility.
- Update CI artifact paths if output locations change.

## Related pages

- @ref project_testing
- @ref project_validation
- @ref project_static_analysis
