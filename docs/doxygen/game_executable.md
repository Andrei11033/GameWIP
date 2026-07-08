@page project_game_executable Game executable

The `game/` tree owns the GameWIP executable boundary. It connects reusable libraries, optional startup validation, generated version metadata, and the current runtime facade into one process.

This page explains the executable and integration layer. It does not replace the validation, testing, benchmarking, build, or library manuals.

## Scope

Use this page when you need to understand or change:

- The process entry point in `game/main.cpp`.
- The runtime facade in `game/runtime/`.
- Startup validation wiring.
- Generated executable version metadata.
- The distinction between executable integration code and reusable libraries.
- Source-level documentation expectations for `game/` files.

Validation module authoring is documented in @ref project_testing. Validation runner behavior is documented in @ref project_validation. Benchmark authoring is documented in @ref project_benchmarking.

## Source layout

| Path | Purpose |
| --- | --- |
| `game/main.cpp` | Process entry point and startup sequencing. |
| `game/runtime/` | Runtime facade entered after optional validation and benchmark startup phases. |
| `game/runtime/version.h.in` | Template for generated build and release identity. |
| `game/validation/validation.h` | Compile-time facade that keeps `main.cpp` stable when validation is disabled. |
| `game/validation/types.h` | Shared result types for embedded and standalone validation runners. |
| `game/validation/tests/` | Correctness-test runner, module registry, standalone test executable, and test modules. |
| `game/validation/benchmarks/` | Google Benchmark runner, standalone benchmark executable, and benchmark modules. |
| `game/validation/public_headers/` | Compile-only checks for public header self-containment. |
| `game/validation/installed_consumer/` | Clean installed-package consumer check. |

## Runtime sequence

`main.cpp` should remain small and predictable:

1. Handle process-level utility arguments such as `--version`.
2. Run startup correctness validation when it is compiled into the executable.
3. Return immediately when validation handles a child-process route.
4. Return immediately when startup validation fails.
5. Run startup benchmarks when they are compiled into the executable.
6. Enter `GameWIP::Game::run()`.

Startup validation and startup benchmarks are development-time features. Disabled startup validation should compile to inline no-op functions so the executable entry point does not need preprocessor branches around every validation call.

## Runtime facade

`GameWIP::Game::run()` is the transition point from process startup into game runtime code. It is intentionally small while the engine-facing runtime is still evolving.

Keep `main.cpp` focused on process startup. Put game runtime composition behind `GameWIP::Game::run()` or a more specific runtime component as the executable grows.

## Version metadata

`game/runtime/version.h.in` generates `gamewip/version.h` during configuration. The generated header provides product display text, version number, build number, Git commit, dirty-state, and release-state metadata.

The executable may expose this metadata through process-level utility arguments such as `--version`. Version policy and release interpretation are documented in @ref project_versioning.

## Validation integration

`game/validation/validation.h` is the stable startup facade included by `main.cpp`. Depending on compile-time options, it either forwards to embedded validation runners or returns successful empty results.

Standalone validation executables remain the normal CI and focused local workflow. Startup validation exists to make development builds fail before entering runtime code when enabled.

See @ref project_validation for runner behavior and command-line flags.

## What belongs in `game/`

Use `game/` for executable integration code:

- Process entry-point behavior.
- Runtime composition.
- Startup validation wiring.
- Standalone validation and benchmark executables.
- Public-header and installed-consumer boundary checks.
- Game-facing adaptation that should not live in reusable libraries.

Move code into a reusable library when it becomes general-purpose, independently testable, and useful outside the executable.

## What should stay out of `game/`

Do not put reusable foundation or tooling behavior in `game/` only because the executable is the first consumer.

In particular, avoid adding these to `game/`:

- File, terminal, logging, assertion, or validation-support behavior that belongs to an existing reusable library.
- Platform backend code that belongs under an owning library's backend contract.
- Library-specific examples or troubleshooting guidance.
- Long-term engine systems that belong under `engine/` once that layer owns them.
- Generic CMake infrastructure that belongs under `cmake/`.

## Source comments

`game/` is not an installed reusable library, but its headers and source files should still explain ownership and integration behavior clearly.

Use source comments to document:

- File purpose.
- Stable integration boundaries.
- Startup sequencing.
- Validation route handling.
- Compile-time disabled behavior.
- Generated-file purpose.
- Non-obvious module registration or child-process protocols.

Do not document every local helper, obvious forwarding function, or simple compile-only include file beyond a short file-level purpose comment.

## Add or change checklist

When changing executable or validation integration code:

- Keep `main.cpp` small and sequencing-focused.
- Keep reusable behavior in the owning library.
- Keep validation disabled paths lightweight and dependency-free.
- Update @ref project_validation when runner behavior or command-line flags change.
- Update @ref project_testing when correctness-test authoring rules change.
- Update @ref project_benchmarking when benchmark authoring or runner behavior changes.
- Update @ref project_versioning when generated version metadata changes.
- Add or update source comments when ownership or integration boundaries change.

## Related pages

- @ref project_structure
- @ref project_build
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_versioning
