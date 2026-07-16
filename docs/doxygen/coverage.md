@page project_coverage Coverage workflow

Coverage is an opt-in validation workflow that reports which correctness-test paths executed.

## Scope

This page documents the coverage preset, generated artifacts, inclusion policy, and common failures.

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

`GAMEWIP_ENABLE_COVERAGE=ON` adds GCC/Clang coverage instrumentation and creates the `coverage` target. The `coverage` preset enables this option and disables the game executable.

The project rejects `GAMEWIP_ENABLE_COVERAGE=ON` when `GAMEWIP_BUILD_TESTS=OFF`.

## Outputs and artifacts

The `coverage` target uses `gcovr` to write:

```text
build/coverage/coverage/index.html
build/coverage/coverage/coverage.xml
```

The report includes maintained foundation libraries, tool libraries, TestSupport, and modular correctness-test sources. Third-party sources under `external/` are excluded.

GCC profile updates are atomic so parallel test processes cannot overwrite one another's counters. Corrupt or negative profile data is a report failure. The workflow must not suppress parser errors.

## CI behavior

The Validation workflow runs the coverage preset on Windows and uploads the HTML/XML output as a build artifact.

Coverage values are not currently percentage gates. A lower coverage number is not automatically a failure unless the workflow, report generation, or tests fail.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Configure fails. | Coverage was enabled without standalone tests. | Use the `coverage` preset or enable `GAMEWIP_BUILD_TESTS`. |
| The `coverage` target is missing. | The project was not configured with coverage instrumentation. | Reconfigure with `cmake --preset coverage`. |
| The report contains corrupt or negative profile data. | Test processes produced invalid coverage counters. | Treat the report as failed and investigate the affected test or toolchain behavior. |
| Third-party files appear in the report. | The exclusion list is incomplete. | Update the coverage helper to exclude vendor and generated paths. |

## Maintainer notes

When changing coverage behavior:

- Keep coverage separate from normal validation.
- Keep third-party and generated sources excluded.
- Do not replace focused tests with a percentage target.
- Preserve report failure visibility.
- Update CI artifact paths if output locations change.

## Related pages

- @ref project_testing
- @ref project_validation
- @ref project_static_analysis
