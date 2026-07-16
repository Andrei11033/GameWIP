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

The same validation composition checks reviewed shared-library exports, public-header self-containment, generated version output, and clean installed-package consumption.

## Commands

Run all modules directly:

```powershell
.\build\test\GameWIPTests.exe
```

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

Run opt-in human checks:

```powershell
.\build\test\GameWIPTests.exe --test-module=test_support --manual-ui
.\build\test\GameWIPTests.exe --test-module=terminal --manual-ui
.\build\test\GameWIPTests.exe --test-module=logger --logger-popup
```

The complete runner argument contract is owned by @ref project_validation.

## Project command helper

The root `gamewip.bat` launcher opens `scripts/GameWIP.ps1`, a project-scoped command menu for configure, build, test, benchmark, docs, analysis, coverage, ASan, validation command-building, and validation stress workflows.

The tool discovers CMake presets from `CMakePresets.json` and reads project command definitions from `scripts/config/gamewip-commands.psd1`. Add new project actions by updating that catalog instead of accepting arbitrary shell commands.

Every command run prints the exact native command, streams output live, and stores logs under `build/tool-runs/<timestamp>/`. Interactive selections use one-key input with Enter for defaults. Configure and build flows offer the next useful action, such as building after configure, running CTest after a test build, generating coverage after a coverage test run, or running benchmark registration after a benchmark build. Failures print the failed action and focused next steps instead of only returning a native exit code.

Validation and stress actions default `TEMP` and `TMP` to `build/gamewip-temp` so local Windows temp-folder permissions cannot make FileSystem and Logger checks fail spuriously.

The validation command builder helps assemble `GameWIPTests.exe` arguments from the supported runner flags, including `--test-module=<name>`, `--skip-test-module=<name>`, report behavior, verbose output, manual UI checks, Logger popup checks, and TestSupport child-process checks. Stress runs report launched, active, completed, and failed worker counts while they run.

Common non-interactive usage:

```powershell
.\gamewip.bat list
.\gamewip.bat build -Preset test
.\gamewip.bat test -Preset test
.\gamewip.bat wizard
.\gamewip.bat module -Module terminal -BuildIfMissing
.\gamewip.bat stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing
.\gamewip.bat run -ProjectCommand benchmark-dry-run -BuildIfMissing
.\gamewip.bat bundle -Bundle quick
```

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

- A failed expectation does not abort the suite.
- A suite entry point returns nonzero when its recorded result fails.
- The runner catches an exception escaping a module callback, marks the module failed, and continues normal aggregate execution.
- Routed child processes preserve their exact exit code.
- Standalone `GameWIPTests` returns success only when the aggregate result is successful.

## Artifact lifecycle

Use `TestSupport::ScopedTemporaryDirectory` for test workspaces, subsystem logs, generated fixtures, and child artifacts. Cleanup occurs on normal return and exception unwinding but remains best effort; process termination or open native resources can leave diagnostics behind.

Final validation reports belong under the operating-system temporary GameWIP root unless a caller supplies an absolute path. Tests must not create persistent `logs/` or fixture output in the source or build tree.

## Test requirements

Correctness tests must:

- Cover a supported public or maintainer contract rather than an implementation coincidence.
- Be deterministic, order-independent, and repeatable.
- Keep unattended defaults free of UI, prompts, and fatal popups.
- Avoid benchmark loops and elapsed-time pass/fail thresholds.
- Prefer deterministic coordination hooks over sleeps.
- Use bounded waits where operating-system scheduling is unavoidable.
- Isolate files and restore current directory, environment, terminal state, hooks, and singleton configuration.
- Keep child protocols uniquely owned and route them before full-suite execution.
- Preserve exact failure evidence and continue after ordinary expectation failures.
- Add regression coverage for behavior changes and fixed defects.
- Use approved internal hooks only when the public API cannot make the scenario deterministic.

Stress tests may remain correctness tests when they verify invariants rather than speed. Throughput and latency measurements belong in @ref project_benchmarking.

## Public-header and installed-consumer checks

`game/validation/public_headers/` contains one compile-only translation unit per supported consumer entry header. Each file includes its target header first and no other GameWIP header, proving self-containment and include-order independence. Generated shared-library export headers are installed visibility scaffolding rather than direct consumer entry points; these checks exercise them transitively, and installed-consumer validation verifies the complete installed header allowlist.

`game/validation/installed_consumer/` configures and builds against the installed package surface only. It verifies installed header usability, representative cross-library integration, the current imported targets after all packages are found, and the absence of source-tree test-hook definitions.

The combined consumer verifies cross-library integration. Separate isolated consumers call only one `find_package()` for each package, proving that higher-level configs discover every imported dependency in their exported interface. Additional cases cover split-prefix runtime Assert and disabled/interface-only Assert.

CI runs every package case with the single supported CMake 4.4 line. One package-validation job covers Ninja and Ninja Multi-Config consumers, and the root requirement is propagated into each independently configured consumer instead of being duplicated.

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
