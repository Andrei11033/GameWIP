@page project_game_executable Game executable

The `game/` tree owns the GameWIP executable boundary. It connects generated build identity, optional startup validation, benchmark startup, and the runtime facade into one process.

This page owns process startup and executable integration. Correctness-runner behavior is documented in @ref project_validation, test-module authoring in @ref project_testing, and benchmark behavior in @ref project_benchmarking.

## Scope

Use this page when changing:

- `game/main.cpp` startup sequencing.
- The executable-owned runtime facade in `game/runtime/`.
- Generated version metadata and `--version` output.
- The compile-time facade in `game/validation/validation.h`.
- The boundary between executable integration code and reusable libraries.

## Source layout

| Path | Purpose |
| --- | --- |
| `game/main.cpp` | Process entry point and startup sequencing. |
| `game/runtime/game.h` | Source-tree runtime facade called after startup work succeeds. |
| `game/runtime/game.cpp` | Runtime facade implementation. |
| `game/runtime/version.h.in` | Template for generated build and release identity. |
| `game/validation/validation.h` | Compile-time facade that keeps `main.cpp` stable when startup validation is disabled. |
| `game/validation/types.h` | Result types shared by embedded and standalone validation runners. |
| `game/validation/tests/` | Correctness runner, module registry, standalone executable, and modules. |
| `game/validation/benchmarks/` | Google Benchmark runner, standalone executable, and benchmark modules. |
| `game/validation/public_headers/` | Compile-only public-header self-containment checks. |
| `game/validation/installed_consumer/` | Clean installed-package consumer check. |

## Runtime sequence

`main.cpp` follows one process-level sequence:

1. In Tracy-enabled builds, name the main thread, open the process zone, and
   wait 500 milliseconds so the profiler can attach before startup work.
2. When the only user argument is `--version`, print
   `GameWIP::Version::productDisplay` and exit successfully.
3. Run startup correctness validation when it was compiled into the executable.
4. Return the validation child-route exit code immediately when
   `handledChildInvocation` is true.
5. Stop startup when correctness validation fails.
6. Run startup benchmarks when they were compiled into the executable.
7. Stop startup when Google Benchmark rejects its arguments.
8. Return the exit code from `GameWIP::Game::run()`.

Utility-only version queries intentionally bypass validation and runtime startup. A `--version` token combined with other arguments is not treated as the utility-only form.

Tracy-enabled builds emit frame marks before startup validation, startup
benchmarks, and runtime execution. The enclosing named zones identify those
process phases in a capture.

There is no process-wide exception boundary around startup validation, benchmark execution, or `GameWIP::Game::run()`. The correctness runner converts exceptions escaping module callbacks, but its outer setup/allocation work and the benchmark runner may still propagate. Runtime code should express expected startup or shutdown failures through its returned exit code. Any exception that reaches `main()` follows the language runtime's uncaught-exception behavior.

## Runtime facade

`GameWIP::Game::run(int, char **)` is the executable-owned transition from startup wiring into runtime composition.

- `argc` and `argv` are the original process arguments and are borrowed for the call.
- The returned integer becomes the executable's process exit code.
- Reusable behavior must remain in its owning foundation or tools library rather than accumulating behind this facade.

`run()` calls Logger console initialization at Debug severity, reports runtime
startup, reports shutdown, and shuts Logger down before returning
`EXIT_SUCCESS`. It does not retain or interpret `argc` or `argv`. Tracy-enabled
builds mark the runtime, Logger initialization, and Logger shutdown phases.

Use @ref GameWIP::Game for the generated source API reference.

## Version metadata

CMake configures `game/runtime/version.h.in` into `gamewip/version.h` during project configuration and refreshes it before building the game executable.

@ref GameWIP::Version exposes:

| Name | Meaning |
| --- | --- |
| `number` | Numeric project version. |
| `display` | Human-readable release or development version. |
| `productDisplay` | Complete product line printed by `--version`. |
| `buildNumber` | Repository-derived build count or configured fallback. |
| `gitCommit` | Generation-time abbreviated commit or fallback. |
| `dirty` | Whether tracked source changes were detected when identity was generated. |
| `release` | Whether generation observed the expected clean annotated release tag. |

Configuration creates the initial values, and the game version-header target refreshes them whenever the executable is built. A long-lived build tree therefore observes newer repository state on rebuild without requiring reconfiguration. Release interpretation is owned by @ref project_versioning.

## Startup validation facade

`game/validation/validation.h` is included unconditionally by `main.cpp`.

- `GAMEWIP_STARTUP_TESTS_ENABLED` controls whether `runTests()` forwards to the correctness runner.
- `GAMEWIP_STARTUP_BENCHMARKS_ENABLED` controls whether `runBenchmarks()` forwards to the benchmark runner.
- Each macro defaults to `0` when the including target does not define it.
- Disabled functions return successful empty results and do not retain runner dependencies.

These definitions are target-private composition controls, not installed configuration API. Use @ref GameWIP::Validation for the generated result and facade reference.

## Source API boundary

The generated manual includes the explicitly registered headers that connect `game/` components. They are documented source-tree interfaces for contributors and validation modules; they are not installed package APIs or long-term binary compatibility promises.

The documented namespaces are:

- @ref GameWIP::Game for the runtime facade.
- @ref GameWIP::Version for generated process identity.
- @ref GameWIP::Validation for shared validation results and startup facade.
- @ref GameWIP::Validation::Tests for correctness-runner and module-registration contracts.
- @ref GameWIP::Validation::Benchmarks for benchmark-runner integration.
- @ref GameWIP::Test for source-tree library self-test entry points and option types.

Private helpers, module-local functions, and approved internal test seams remain implementation or maintainer interfaces and are not presented as installed consumer API.

## Dependency boundary

`game/` may compose reusable libraries, validation objects, Google Benchmark, Tracy instrumentation, and generated project metadata. It must not become the implementation owner for behavior that belongs to an existing library.

Move code into a reusable library when it becomes general-purpose, independently testable, and useful outside the executable. Long-term engine systems belong under `engine/` when that layer owns them.

## Source comments

Every important `.h`, `.cpp`, and generated-header template under `game/` starts with `@file` and `@brief` documentation.

Source comments should explain:

- Ownership and integration boundaries.
- Startup and shutdown sequencing.
- Compile-time disabled behavior.
- Registration lifetime and module adaptation.
- Child-process routing protocols.
- Process-global or generated state.
- Non-obvious validation and benchmark framework requirements.

Do not narrate simple assignments, forwarding calls, obvious test expectations, or compile-only includes beyond their file-level purpose.

## Review checklist

When changing executable integration:

- Keep `main.cpp` small and sequencing-focused.
- Preserve utility-only version behavior.
- Keep disabled validation paths dependency-free.
- Return child-route results before benchmarks and runtime code.
- Keep expected runtime failures representable as process exit codes.
- Update @ref project_validation for runner or command-line changes.
- Update @ref project_testing for test-module contract changes.
- Update @ref project_benchmarking for benchmark-runner changes.
- Update @ref project_versioning for generated identity changes.
- Update registered source API comments when contracts change.

## Related pages

- @ref project_structure
- @ref project_build
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_versioning
- @ref project_documentation
