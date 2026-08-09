@page project_testing Correctness testing

Correctness tests answer whether behavior is correct. They must not contain benchmark loops, machine-dependent timing thresholds, or performance-regression policy.

## Scope

This page owns test-module structure, source interfaces, authoring rules, reports, artifacts, manual checks, and validation coverage expectations.

Runner architecture and command-line ownership are documented in @ref project_validation. Performance measurements are documented in @ref project_benchmarking. Library-specific test coverage and approved hooks remain documented in each library manual.

## Common workflow

Configure, build, and run all validation through CTest:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

The same validation composition checks shared-library exports, public-header
self-containment, generated version output, and clean installed-package
consumption.

## Commands

Run all modules directly:

```powershell
.\build\test\GameWIPTests.exe
```

CTest registers one entry per correctness module so failures remain focused and
modules can be selected by name. It does not add a second all-module entry; the
direct aggregate form above is reserved for explicit local, stress, and embedded
startup use.

CTest presets and CI fail when discovery selects zero tests. Project contracts
have two-minute bounds, ordinary modules have five-minute bounds, and installed
package consumers have ten-minute bounds for nested configure/build work. These
limits detect hangs; they are deliberately not performance thresholds.

Run one module and retain a focused report:

```powershell
.\build\test\GameWIPTests.exe `
  --test-module=filesystem `
  --test-report=logs/tests/filesystem_test_report.txt
```

Disable the retained report for quick local iteration:

```powershell
.\build\test\GameWIPTests.exe --test-module=logger --no-test-report
```

Mirror complete suite output to stdout:

```powershell
.\build\test\GameWIPTests.exe --test-module=logger --verbose-tests --no-test-report
```

Run all modules except one project module:

```powershell
.\build\test\GameWIPTests.exe --skip-test-module=terminal
```

Run opt-in manual checks:

```powershell
.\build\test\GameWIPTests.exe --test-module=test_support --manual-tests
.\build\test\GameWIPTests.exe --test-module=terminal --manual-tests
.\build\test\GameWIPTests.exe --test-module=logger --manual-tests
.\build\test\GameWIPTests.exe --test-module=window --manual-tests
```

Each selected module owns its manual scenarios. The Window module groups the full visible, shell, DPI, cursor, fullscreen, topology, HDR, and modern-capability workflow from @ref window_manual_validation. Unsupported or unavailable environments are recorded as skips and are not passing evidence.

Window manual runs open a live diagnostics companion. A module-specific `--window-manual-suite=<name>` selector may narrow a repeat run without creating another project-wide manual enable flag; the selector contract is owned by @ref window_testing.

The complete runner argument contract is owned by @ref project_validation.

## Project command helper

The root `gamewip.bat` launcher opens `scripts/GameWIP.ps1`, a project-scoped command menu for configure, build, test, formatting, Unicode data maintenance, benchmarks, documentation, static analysis, coverage, AddressSanitizer, validation command-building, and validation stress workflows.

The interactive `Q` menu groups quality and maintenance actions. The `U` menu owns Unicode data status, verification, and regeneration. Non-interactive `format`, `analyze`, `asan`, `coverage`, `benchmark`, and `docs` actions expose the same project-owned workflows directly.

The tool discovers CMake presets from `CMakePresets.json` and reads project command definitions from `scripts/config/gamewip-commands.psd1`. Add new project actions by updating that catalog instead of accepting arbitrary shell commands.

Every command run prints the exact native command, streams output live, and stores logs under `build/tool-runs/<timestamp>/`. Interactive selections use one-key input with Enter for defaults. Configure and build flows offer the next useful action, such as building after configure, running CTest after a test build, generating coverage after a coverage test run, or running benchmark registration after a benchmark build. Failures print the failed action and focused next steps instead of only returning a native exit code.

Validation and stress actions default `TEMP` and `TMP` to `build/gamewip-temp` so local Windows temp-folder permissions cannot make FileSystem and Logger checks fail spuriously.

The validation command builder helps assemble `GameWIPTests.exe` arguments from the supported runner flags, including `--test-module=<name>`, `--skip-test-module=<name>`, report behavior, verbose output, manual tests, and TestSupport child-process checks. Stress runs report launched, active, completed, and failed worker counts while they run.

Common non-interactive usage:

```powershell
.\gamewip.bat doctor
.\gamewip.bat unicode -UnicodeAction status
.\gamewip.bat unicode -UnicodeAction verify
.\gamewip.bat format -FormatAction check
.\gamewip.bat format -FormatAction apply
.\gamewip.bat analyze
.\gamewip.bat asan
.\gamewip.bat git
.\gamewip.bat git -GitAction status
.\gamewip.bat git -GitAction switch -GitBranch feature/example
.\gamewip.bat workflow -WorkflowAction list
.\gamewip.bat workflow -WorkflowAction run -Workflow validation -Preview
.\gamewip.bat workflow -WorkflowAction status
.\gamewip.bat list
.\gamewip.bat build -Preset test
.\gamewip.bat test -Preset test
.\gamewip.bat wizard
.\gamewip.bat module -Module terminal -BuildIfMissing
.\gamewip.bat stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing
.\gamewip.bat run -ProjectCommand benchmark-dry-run -BuildIfMissing
.\gamewip.bat bundle -Bundle quick
```

`doctor` checks repository metadata and the exact UCRT64/CLANG64 tools used by project commands, including the Python and clang-format executables required by maintenance workflows. Build and test actions automatically configure a missing build tree. All normal presets explicitly use `C:\MSYS2\ucrt64\bin`; ASan uses `C:\MSYS2\clang64\bin`, so an unrelated CMake or compiler earlier on the user's global `PATH` cannot silently change the build.

`gamewip.bat git` opens the guarded Git workspace menu. It shows concise status,
fetches and prunes remote references, switches between local or fetched remote
branches, creates branches, shows recent history, fast-forwards the current
tracked branch, and pushes or publishes it with an upstream. Its cleanup flow
offers safe deletion for ancestry-merged local branches. A branch whose upstream
is gone after a squash merge is never assumed safe: force deletion requires a
separate explicit confirmation, and the current/default/common integration
branches are protected.

`gamewip.bat workflow` opens a separate GitHub workflow menu backed by the
declarative `ManualWorkflows` catalog. The helper supports validation, project
reconciliation dry-run/write modes, release check/prepare/finalize modes, and
Doxygen Pages deployment. It always dispatches from `master`, prints the exact
`gh workflow run`, `gh run watch`, and verification commands, discovers the
queued run, and can watch it through completion.

Use `-Preview` to validate constructed commands without GitHub authentication
or network access. Checks and dry runs use a yes/no confirmation. Writes,
deployments, and finalization use an operation-specific typed phrase; manually
dispatched project and release writes also wait for GitHub protected-environment
approval. Finalization requires the complete 40-character master commit SHA.
Arbitrary workflow names, refs, and input flags are intentionally unsupported.

Running `gamewip.bat` without arguments opens the interactive menu. The helper is intentionally project-scoped: stress and project-command actions are selected from known GameWIP validation, benchmark, and executable checks instead of accepting arbitrary shell commands.

## Test-module source API

Use @ref GameWIP::Test for the generated reference to each source-tree test option type and suite entry point.

The module headers are validation interfaces, not installed library APIs. Each one provides:

- A module-specific options structure with unattended defaults.
- A `run...Tests()` entry point returning zero for pass and nonzero for failure.
- Borrowed process arguments when the module owns a child protocol; a direct caller must keep `argv` and its pointed-to strings valid for the call, and a module must copy any value it retains.
- TestSupport report settings passed through the module adapter.

The shared validation runner may replace module-local defaults with `RunOptions`. Code that invokes a module entry point directly is responsible for supplying suitable report paths, restoring process-global state, and interpreting child arguments consistently.

## Module standard

Each correctness module owns:

```text
game/validation/tests/<module>/
  CMakeLists.txt
  module.cpp
  <module>_test.h
  <module>_test.cpp
```

- `<module>_test.h` defines the source-tree options and entry point.
- `<module>_test.cpp` owns suites, fixtures, child behavior, and TestSupport reporting.
- `module.cpp` maps shared runner policy and creates one static registration.
- `CMakeLists.txt` explicitly lists sources and linked libraries.

Example registration:

```cpp
const GameWIP::Validation::Tests::Registration registration({
    .name = "filesystem",
    .order = 20,
    .run = run,
});
```

The registration name must match the CMake module name. Add a child matcher only when the module owns a distinct child-process protocol.

## Reports and exit behavior

Normal validation uses concise console output: failures, skips, manual instructions, one module result, and one aggregate result. `--verbose-tests` additionally mirrors passing checks, informational lines, metrics, stress diagnostics, suite results, and summaries.

The retained report receives complete TestSupport output when file reporting is enabled. An invalid report path disables only retained output; it must not hide console failures or change test counts.

TestSupport infrastructure helpers keep setup and operating-system failures separate from the behavior under test. Callers must inspect a result's `status` before using its payload. For child processes, infrastructure `status`, process `outcome`, and exact `exitCode` are independent: a nonzero child exit or an expected timeout is a domain outcome, not an infrastructure failure. Use `TestSupport::formatInfrastructureStatus()` only when a failed status needs to be recorded in diagnostics. See @ref test_support_public_api, @ref test_support_child_processes, and @ref test_support_files_environment for the complete API contract.

- A failed expectation does not abort the suite.
- A suite entry point returns nonzero when its recorded result fails.
- The runner catches an exception escaping a module callback, marks the module failed, and continues normal aggregate execution.
- Routed child processes preserve their exact exit code.
- Standalone `GameWIPTests` returns success only when the aggregate result is successful.

## Artifact lifecycle

Use `TestSupport::ScopedTemporaryDirectory` for test workspaces, subsystem logs, generated fixtures, and child artifacts. Construction is non-throwing and fallible, so inspect `status()` before using `path()`; a failed guard is inert and exposes an empty path. Cleanup occurs on normal return and exception unwinding but remains best effort; process termination or open native resources can leave diagnostics behind. Apply the same status-first rule to current-directory and environment guards before assuming their requested process-state change took effect.

Final validation reports belong under the operating-system temporary GameWIP root unless a caller supplies an absolute path. Tests must not create persistent `logs/` or fixture output in the source or build tree.

## Test requirements

Correctness tests must:

- Cover a supported public or maintainer contract rather than an implementation coincidence.
- Be deterministic, order-independent, and repeatable.
- Keep unattended defaults free of UI, prompts, and fatal popups.
- Avoid benchmark loops and elapsed-time thresholds used as performance gates.
- Prefer deterministic coordination hooks over sleeps.
- Use bounded waits where operating-system scheduling is unavoidable.
- Use a deliberately generous elapsed-time bound only to verify an owned
  timeout contract when no deterministic observation can prove that the
  operation returned.
- Isolate files and restore current directory, environment, terminal state, hooks, and singleton configuration.
- Keep child protocols uniquely owned and route them before full-suite execution.
- Preserve exact failure evidence and continue after ordinary expectation failures.
- Inspect TestSupport infrastructure status before consuming returned text, boolean, count, path, or child-process fields.
- Add regression coverage for behavior changes and fixed defects.
- Use approved internal hooks only when the public API cannot make the scenario deterministic.

Stress tests may remain correctness tests when they verify invariants rather than speed. Throughput and latency measurements belong in @ref project_benchmarking.

## Public-header and installed-consumer checks

`game/validation/public_headers/` contains one compile-only translation unit per supported consumer entry header. Each file includes its target header first and no other GameWIP header, proving self-containment and include-order independence. Generated shared-library export headers are installed visibility scaffolding rather than direct consumer entry points; these checks exercise them transitively, and installed-consumer validation verifies the complete installed header allowlist.

`game/validation/installed_consumer/` configures and builds against the installed package surface only. It verifies installed header usability, representative cross-library integration, the current imported targets after all packages are found, and the absence of source-tree test-hook definitions.

The combined consumer verifies cross-library integration. Separate isolated consumers call only one `find_package()` for each package, proving that higher-level configs discover every imported dependency in their exported interface. The combined and isolated TestSupport cases compile and run representative status, formatting, and child-result contracts while explicitly rejecting `INTERNAL_TEST_SUPPORT_TEST_HOOKS`. Additional cases cover split-prefix runtime Assert and disabled/interface-only Assert.

The dedicated `Packages (CMake)` job owns ordinary package compatibility across
Ninja and Ninja Multi-Config, so `Build and Test` excludes package-labeled CTest
entries instead of repeating the single-config cases. Coverage and
AddressSanitizer still run their package entries because those executions prove
that separately instrumented consumers link and run. The root CMake 4.4.2
requirement is propagated into each independently configured consumer rather
than copied into consumer source.

These checks complement runtime suites; they do not replace behavior validation.

## Manual and interactive checks

Manual checks must be opt-in and provide clear instructions, pass/fail/skip input, and retained reporting. Default CTest, startup validation, coverage, and sanitizer runs must not require a real console, display dialogs, wait for keyboard input, or show fatal popups.

## Source documentation

Large `_test.cpp` files do not require Doxygen comments on every local helper or test case. File-level documentation, descriptive function names, and focused comments around child protocols, global-state restoration, concurrency coordination, abnormal termination, and platform limitations are the preferred standard.

Module headers and adapters require complete contract comments because they are shared source interfaces between validation components.

## Maintainer notes

When adding tests:

- Start from the contract being guaranteed.
- Put reusable fixtures in TestSupport only when multiple modules need them.
- Keep module adapters thin.
- Reset approved hook state before and after mutation.
- Record focused commands in the pull request.
- Update the owning library manual when a test reveals an undocumented public contract.

## Related pages

- @ref project_validation
- @ref project_benchmarking
- @ref project_coverage
- @ref project_extending
- @ref project_documentation
