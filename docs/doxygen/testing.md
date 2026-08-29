@page project_testing Correctness testing

Correctness tests answer whether behavior is correct. They must not contain benchmark loops, machine-dependent timing thresholds, or
performance-regression policy.

This guide explains how correctness tests are divided into modules and suites,
how they use shared source interfaces, what a useful test must prove, and how
reports, artifacts, child scenarios, and manual checks fit together.

Runner architecture and command-line ownership are documented in @ref project_validation. Performance measurements are documented in @ref
project_benchmarking. Library-specific test coverage and approved hooks remain documented in each library manual.

## Common workflow

Configure, build, and run all validation through CTest:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

For the same workflow through the project helper, use an incremental run during
ordinary iteration or request a complete preset-tree recreation explicitly:

```powershell
.\gamewip.bat test test
.\gamewip.bat test test -Fresh
```

Fresh mode cannot be combined with `-NoBuild`, because deleting the selected
tree makes configuration and compilation mandatory. Installed-package consumer
tests continue to create their own isolated consumer build directories. CI
jobs run in fresh hosted workspaces and do not restore CMake build trees.

The same validation composition checks shared-library exports, public-header
self-containment, generated version output, runtime dependency staging, and
clean installed-package consumption. On the supported Windows/MSYS2 GNU
configuration, `validation.cmake.runtime_dependencies` proves that the active
compiler runtime replaces a stale app-local DLL and that the staged executable
launches.

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
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests
```

Each selected module owns its manual scenarios. The Desktop module groups the full visible, shell, DPI, cursor, fullscreen, topology, HDR, and
modern-capability workflow from @ref desktop_manual_validation. Unsupported or unavailable environments are recorded as skips and are not passing
evidence.

Desktop manual runs open a live diagnostics companion. A module-specific `--desktop-manual-suite=<name>` selector may narrow a repeat run without
creating another project-wide manual enable flag; the selector contract is owned by @ref desktop_testing.

The complete runner argument contract is owned by @ref project_validation.

## Project command helper

The root `gamewip.bat` helper provides interactive and noninteractive validation
commands, a command builder, focused modules, and bounded parallel stress runs.
Its complete syntax, defaults, catalog, retained-output behavior, failure model,
and extension rules are owned by @ref project_command_line_tools.

Use `GameWIPTests.exe --help` to print current public runner options and module
names without executing validation. The complete runner argument contract
remains owned by @ref project_validation.

## Test-module source API

Use @ref GameWIP::Test for the generated reference to each source-tree test option type and suite entry point.

The module headers are validation interfaces, not installed library APIs. Each one provides:

- A module-specific options structure with unattended defaults.
- A `run...Tests()` entry point returning zero for pass and nonzero for failure.
- Borrowed process arguments when the module owns a child protocol; a direct caller must keep `argv` and its pointed-to strings valid for the call,
  and a module must copy any value it retains.
- TestSupport report settings passed through the module adapter.

The shared validation runner may replace module-local defaults with `RunOptions`. Code that invokes a module entry point directly is responsible for
supplying suitable report paths, restoring process-global state, and interpreting child arguments consistently.

## Module standard

Each correctness module owns one logical registration. Large suites may organize coherent behavioral case bodies into private included fragments when
that keeps fixtures translation-unit-local, or into focused translation units when a real shared private test contract already exists:

```text
game/validation/tests/<module>/
  CMakeLists.txt
  module.cpp
  <module>_test.h
  <module>_test.cpp
  <behavior>_test.inl           # optional TU-local focused case fragment
  <behavior>_test.cpp           # optional focused translation unit when justified
```

- `<module>_test.h` defines the source-tree options and entry point.
- `<module>_test.cpp` owns the module-level TestSupport runner, shared TU-local fixtures/helpers, and suite registration calls.
- Focused `<behavior>_test.inl` files may hold large coherent suite bodies included inside the module's private namespace. Prefer this form when
  separate translation units would require duplicating fixtures or manufacturing a broad private declaration surface.
- Focused `<behavior>_test.cpp` files are appropriate when the cases already have a clean independently compilable private boundary. Do not split
  small suites mechanically.
- `module.cpp` maps shared runner policy and creates one static registration.
- `CMakeLists.txt` explicitly lists compiled sources and linked libraries; private included fragments do not become separate validation modules.

Splitting case files does not create new validation modules, executables, report contracts, or registration names. Promote a fixture to TestSupport
only when it is genuinely reusable across modules rather than merely shared by files inside one module.

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

Normal validation uses concise console output: failures, skips, manual instructions, one module result, and one aggregate result. `--verbose-tests`
additionally mirrors passing checks, informational lines, metrics, stress diagnostics, suite results, and summaries.

The retained report receives complete TestSupport output when file reporting is enabled. An invalid report path disables only retained output; it must
not hide console failures or change test counts.

TestSupport infrastructure helpers keep setup and operating-system failures separate from the behavior under test. Callers must inspect a result's
`status` before using its payload. For child processes, infrastructure `status`, process `outcome`, and exact `exitCode` are independent: a nonzero
child exit or an expected timeout is a domain outcome, not an infrastructure failure. Use `TestSupport::formatInfrastructureStatus()` only when a
failed status needs to be recorded in diagnostics. See @ref test_support_public_api, @ref test_support_child_processes, and @ref
test_support_files_environment for the complete API contract.

- A failed expectation does not abort the suite.
- A suite entry point returns nonzero when its recorded result fails.
- The runner catches an exception escaping a module callback, marks the module failed, and continues normal aggregate execution.
- Routed child processes preserve their exact exit code.
- Standalone `GameWIPTests` returns success only when the aggregate result is successful.

## Artifact lifecycle

Use `TestSupport::ScopedTemporaryDirectory` for test workspaces, subsystem logs, generated fixtures, and child artifacts. Construction is non-throwing
and fallible, so inspect `status()` before using `path()`; a failed guard is inert and exposes an empty path. Cleanup occurs on normal return and
exception unwinding but remains best effort; process termination or open native resources can leave diagnostics behind. Apply the same status-first
rule to current-directory and environment guards before assuming their requested process-state change took effect.

Final validation reports belong under the operating-system temporary GameWIP root unless a caller supplies an absolute path. Tests must not create
persistent `logs/` or fixture output in the source or build tree.

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

Stress tests may remain correctness tests when they verify invariants rather than speed. Throughput and latency measurements belong in @ref
project_benchmarking.

## Public-header and installed-consumer checks

`game/validation/public_headers/` contains one compile-only translation unit per supported consumer entry header. Each file includes its target header
first and no other GameWIP header, proving self-containment and include-order independence. Generated shared-library export headers are installed
visibility scaffolding rather than direct consumer entry points; these checks exercise them transitively, and installed-consumer validation verifies
the complete installed header allowlist.

`game/validation/installed_consumer/` configures and builds against the installed package surface only. It verifies installed header usability,
representative cross-library integration, the current imported targets after all packages are found, and the absence of source-tree test-hook
definitions.

The combined consumer verifies cross-library integration. Separate isolated consumers call only one `find_package()` for each package, proving that
higher-level configs discover every imported dependency in their exported interface. The combined and isolated TestSupport cases compile and run
representative status, formatting, and process-result contracts while explicitly rejecting `TEST_SUPPORT_INTERNAL_TEST_HOOKS`. Focused installed
consumers also compile `test_support/types.h`, `reporting.h`, `files.h`, `process.h`, and `stress.h` independently. Additional cases cover
split-prefix runtime Assert and disabled/interface-only Assert.

The dedicated `Packages (CMake)` job owns ordinary package compatibility across
Ninja and Ninja Multi-Config, so `Build and Test` excludes package-labeled CTest
entries instead of repeating the single-config cases. Coverage and
AddressSanitizer still run their package entries because those executions prove
that separately instrumented consumers link and run. The root CMake 4.4.2
requirement is propagated into each independently configured consumer rather
than copied into consumer source.

These checks complement runtime suites; they do not replace behavior validation.

## Manual and interactive checks

Manual checks must be opt-in and provide clear instructions, pass/fail/skip input, and retained reporting. Default CTest, startup validation,
coverage, and sanitizer runs must not require a real console, display dialogs, wait for keyboard input, or show fatal popups.

## Source documentation

Focused correctness case files do not require Doxygen comments on every local helper or test case. File-level documentation, descriptive function
names, and focused comments around child protocols, global-state restoration, concurrency coordination, abnormal termination, and platform limitations
are the preferred standard.

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
