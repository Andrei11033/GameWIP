@page project_decisions Project decisions

This page records stable project-wide decisions. It is not a changelog, implementation tracker, test catalog, library manual, or roadmap.

Use:

- @ref project_vision for product identity.
- @ref project_roadmap for milestone completion criteria.
- GitHub issues for active tasks, bugs, validation work, and follow-up cleanup.
- @ref project_platform_backend_contract for platform-backend rules.
- @ref project_public_api_contract for project-wide public naming, text, result, performance, and compatibility rules.
- @ref project_versioning for version policy.
- @ref project_contributing for GitHub workflow and merge messages.
- The owning library manual for library-specific behavior and API contracts.

## Product direction

GameWIP is a sandbox building game centered on player-made vehicles, weapons, missiles, buildings, technical systems, and meaningful destruction.

Building should be quick to start and useful with defaults. Engineering depth should be optional and added through configuration rather than required setup.

Damage must affect both structure and function. Parts may weaken, detach, fail, expose internals, or change connected system behavior. Realism is valuable when it creates understandable engineering choices; usability takes priority when realism only adds friction.

## Simulation and presentation

Simulation uses a fixed timestep and remains separate from rendering. Behavior must not depend on render frame rate.

Only systems with a demonstrated need use higher-frequency updates. Ordinary gameplay does not inherit the highest simulation rate by default.

Rendering supports development, debugging, and validation before presentation
polish. Foundational simulation, visibility, and correctness take priority over
visual refinement during foundational development.

## Toolchain and platform

- The project language standard is C++23 without compiler extensions.
- CMake and Ninja own configuration and builds.
- Visual Studio Code is the recommended editor and owns repository-scoped workflow integration. Visual Studio Community is an optional selected IDE, not a compiler prerequisite.
- Windows with MSYS2 UCRT64 GCC is the normal development environment.
- MSYS2 CLANG64 is used for AddressSanitizer validation.
- The root setup entry point owns reproducible installation, update, repair, editor integration, and environment verification on Windows 11.
- The repository is Windows-first, but reusable public APIs remain portable unless a platform concept is itself the contract.
- Public/project text is UTF-8 and continues to use `std::string`/`std::string_view` rather than `std::u8string`.
- Encoding-agnostic data is called bytes rather than text. Byte-oriented IO remains independent of Unicode.
- GameWIP does not automatically normalize Unicode, insert or remove a BOM, or silently repair invalid text.
- Validate text at trust and native boundaries, combining conversion and validation where practical; do not blindly repeat full scans on trusted hot forwarding paths.
- Win32 backends convert at the operating-system boundary and use wide-character APIs where required.

## Repository architecture

- `foundation/` owns low-level reusable runtime libraries.
- `tools/` owns reusable diagnostics, assertions, logging, validation support, and development tooling.
- `engine/` owns engine systems reviewed separately from the reusable foundation and tool libraries.
- `game/` owns the process entry point, runtime facade, and validation executable wiring.
- `cmake/` owns project-wide build orchestration and shared CMake helpers.
- `docs/doxygen/` owns generated project-manual pages.
- `docs/` owns product direction, roadmap, decisions, versioning, and contributor workflow records.
- `external/` owns pinned third-party dependencies and should not be rewritten by project formatting or documentation passes.

Dependency direction is documented in @ref project_structure.

## Reusable library standard

Reusable libraries must be independently buildable, testable, installable, and consumable from a clean external CMake project through their installed package boundary.

Standalone does not mean anonymous ownership. First-party installed targets intentionally use the `GameWIP::` namespace.

A reusable library owns its public API, package boundary, docs, tests, platform backend, and compatibility notes. Public headers must expose portable types and must not require consumers to include internal headers, platform headers, validation hooks, or game-runtime types.

Performance is part of reusable API design. Preserve reusable caller storage, avoid success-path diagnostic allocation and redundant scans, keep optional features lazy, and benchmark meaningful hot paths without turning benchmark timings into correctness gates.

## Build and packaging

- Root presets define supported project workflows.
- Library CMake files own their public target, sources, package config, install rules, and documentation registration.
- Project CMake helpers define common policy and integration patterns.
- Public dependencies must be declared as public package dependencies.
- Implementation-only dependencies must remain private.
- Package compatibility is validated through public-header checks and clean installed-consumer workflows.

Installed packages assume ABI-compatible C++23 toolchains, standard libraries, runtimes, architectures, configurations, and exact GameWIP versions. The project does not promise a universal stable C ABI.

Pre-1.0 public APIs may be corrected directly instead of retaining deprecated aliases unless a specific compatibility requirement justifies a transition layer.

Umbrella public headers may remain supported while focused headers are added. Physical header decomposition does not require namespace or package decomposition and should be justified by measured compile-time or usability value.

Detailed CMake rules are documented in @ref project_cmake_infrastructure. Package rules are documented in @ref project_library_compatibility.

## Validation policy

Correctness tests must validate behavior, not timing. Benchmarks measure performance and registration health, not correctness thresholds.

Validation modules use stable lowercase names and register through the shared validation runner. Source-tree-only hooks may be used only when public APIs cannot make a scenario deterministic.

Manual checks are opt-in. CI should remain unattended unless a workflow explicitly documents a human-gated step.

## Documentation ownership

Doxygen is the generated developer manual for contributors, maintainers, and reusable-library consumers. It is not player-facing game documentation.

Project workflow and contract pages live under `docs/doxygen/`. Product direction, roadmap, decisions, versioning, and contributor workflow records live under `docs/`. Library manuals live under each library's `docs/` directory.

Documentation rules are documented in @ref project_documentation.

## Repository workflow

Feature work normally uses an issue, short-lived branch, pull request, required validation evidence, and squash merge.

The seven protected `master` checks are the authoritative pre-merge CI gate.
Manual validation dispatches rerun that gate for diagnostics or post-merge
verification; they do not create a second required path. Expensive local quality
workflows remain change-driven, while release preparation uses the documented
release-readiness bundle.

Commit and pull-request titles use:

```text
area: imperative summary
```

The full GitHub workflow, required metadata, automation behavior, and squash message format are documented in @ref project_contributing.
Repository settings, check ownership, manual dispatch policy, and public-release
audits are documented in @ref project_repository_maintenance.

## Licensing and public history

GameWIP first-party source code and documentation use the
[Apache License 2.0](https://github.com/Andrei11033/GameWIP/blob/master/LICENSE).
The root
[NOTICE](https://github.com/Andrei11033/GameWIP/blob/master/NOTICE) records
project attribution. Pinned dependencies under `external/` retain their own
licenses and notices; a future non-code asset may declare a separate license
when its distribution requirements differ from the source repository.

Unless explicitly stated otherwise in writing and accepted by the maintainer,
contributions intentionally submitted for inclusion use Apache-2.0 under the
license's contribution terms. The license permits reuse of published versions;
it does not transfer control of the official repository, releases, settings,
or project identity.

The reachable Git history was reviewed before public activation. Known author
metadata and the historical source-snapshot archive are accepted for public
visibility, so no history rewrite is required. This acceptance does not excuse
a newly discovered credential or sensitive artifact: rotate affected secrets
and sanitize history before public exposure when a real disclosure is found.

## Changing a decision

Update this page only for durable project-wide decisions. Keep the decision concise, update affected workflow or contract pages in the same change, and put implementation work in GitHub issues rather than turning this page into a task list.

## Related pages

- @ref project_vision
- @ref project_roadmap
- @ref project_structure
- @ref project_cmake_infrastructure
- @ref project_extending
- @ref project_versioning
- @ref project_contributing
