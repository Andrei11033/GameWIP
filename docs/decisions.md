@page project_decisions Project decisions

This page records the choices that shape more than one part of GameWIP. It
explains what the project has decided, why that choice matters, and what it
requires from future work.

It is not a changelog or task list. Library-specific behavior belongs in that
library's manual, milestone gates belong in @ref project_roadmap, and active
work belongs in GitHub issues.

## Product and simulation

### Building is approachable; depth is optional

Components should work with useful defaults, so a player can build quickly.
Advanced configuration is added where it creates meaningful engineering
choices, not as a prerequisite for basic use.

This keeps the first experience understandable without placing a low ceiling
on complex vehicles, weapons, guidance, sensors, and control systems. The full
product direction is described in @ref project_vision.

### Damage changes structure and function

Damage is part of the simulation rather than a visual effect. It may change
strength, mass, connectivity, component state, or the behavior of connected
systems. New structural and component designs must account for those
consequences instead of treating destruction as block removal alone.

### Simulation timing does not depend on rendering

Simulation uses a fixed timestep and remains separate from presentation. Only
systems with a demonstrated need receive higher-frequency updates; ordinary
gameplay does not run at the most expensive rate by default.

Rendering is developed early enough to expose and debug the simulation, but
foundational correctness and observability come before presentation polish.

### Early play supports single-player and small-group co-op

GameWIP targets excellent local single-player and small-group cooperative play
before persistent or large-scale multiplayer. Networking is nevertheless an
early architectural concern because authority, ownership, and replication
cannot be added safely as an afterthought.

V1 therefore includes server-authoritative small-group multiplayer and a
supported headless or dedicated-server path. Persistent worlds and larger
populations remain a post-V1 direction, and distributed hosting is considered
only after measured limits justify it.

### Authoritative simulation is separate from presentation and hosting

Gameplay simulation owns authoritative state without depending on rendering,
audio, human input, or network transport. Single-player composes presentation
with a local authoritative simulation. Multiplayer places the same gameplay
authority on a host or server.

Single-player does not require serialization, loopback traffic, or a fake
network client/server path. Hosting and transport can change without changing
the meaning of gameplay commands or simulation state.

### Authority boundaries use explicit commands, intents, and state

Gameplay mutations that cross an authority boundary use explicit command or
intent and resulting state boundaries where appropriate. The authority
validates requests and owns the resulting mutation; presentation and remote
clients observe the result.

This makes ownership, replication, rejection, diagnostics, and testing visible
without requiring every internal variable to become network state.

### Identity and schema ownership are explicit

Concepts crossing process, network, save, content, or configuration boundaries
receive stable identity and schema ownership when that boundary appears.
Runtime object identity, network identity, persistent identity, and content
identity remain separate where their meanings differ; GameWIP does not create
one universal ID.

Save, network, content, asset, and configuration schemas version independently
from project SemVer. Each domain gains migration policy when it needs one
instead of inheriting a universal save format or the repository version.

### Scalability follows ownership, relevance, measurement, and evidence

Systems are designed for selective activity: sleeping objects, unchanged
systems, irrelevant entities, and inactive controllers should consume little
work. Networking uses relevance and appropriate update frequency rather than
replicating everything.

Representative workloads are measured before optimization. Large-world and
distributed techniques are introduced in response to demonstrated limits, not
speculative MMO counts.

### Audio observes simulation

Simulation exposes state and events that audio translates into feedback.
Simulation never depends on audio existing, while audio can grow from early
playback and spatial feedback into material, machinery, damage, and acoustic
depth.

This preserves headless and testable simulation while keeping sound integral
to how players understand it.

### World architecture remains partitionable

World systems preserve a boundary between local simulation coordinates and
eventual large-world addressing. Generated base regions are reproducible from
their seed, generation version, and region coordinate, while persistent edits
and entities remain separate state.

The project does not yet select final region dimensions, coordinate technique,
terrain representation, or persistence technology. Those choices follow
product requirements and evidence without closing the path to streaming and
larger persistent worlds.

## Language, toolchain, and platform

### C++23, CMake, and Ninja define the build baseline

First-party C++ uses C++23 without compiler extensions. CMake owns
configuration and build composition, and Ninja is the supported generator for
normal repository workflows.

Windows 11 with MSYS2 UCRT64 GCC is the primary development environment.
MSYS2 CLANG64 provides AddressSanitizer validation. Visual Studio Code is the
recommended editor and owns repository-scoped workflow integration; Visual
Studio Community is optional and is not a compiler prerequisite.

The root setup entry point owns installation, update, repair, editor
integration, and environment verification. The exact supported workflow is in
@ref project_environment_setup.

### Windows is the first backend, not a public-API shortcut

The repository is Windows-first, but reusable public APIs stay portable unless
the platform concept is itself the API. Operating-system types and headers
belong behind internal backends or in explicitly native interop APIs. See
@ref project_platform_backend_contract for the boundary rules.

### Project text is UTF-8

Public text uses UTF-8 stored in `std::string` and `std::string_view`.
Encoding-agnostic data is described as bytes. IO primitives do not guess an
encoding; text-aware operations may depend on Unicode when they enforce the
UTF-8 contract.

GameWIP does not silently normalize Unicode, add or remove a byte-order mark,
or repair invalid input. Validation belongs at trust and native boundaries,
preferably combined with conversion so trusted hot paths do not repeat full
scans. Win32 backends use wide-character APIs where required and convert at the
operating-system boundary.

## Repository and dependency structure

Each top-level area has a distinct job:

- `foundation/` contains low-level reusable runtime libraries.
- `tools/` contains reusable diagnostics, assertions, logging, validation
  support, and development tools.
- `engine/` contains engine systems, reviewed separately from foundation and
  tool libraries.
- `game/` composes those systems at the process and runtime boundary.
- `cmake/` contains project-wide build orchestration and shared helpers.
- `docs/` and library `docs/` directories contain maintained manuals and
  project records.
- `external/` contains pinned third-party code and is excluded from first-party
  formatting and documentation rewrites.

Dependencies should point toward lower-level concepts, never toward a more
specific consumer merely for convenience. @ref project_structure contains the
actual dependency map and allowed exceptions.

## Reusable libraries and public APIs

### A supported library is consumable on its own

Each supported reusable library owns its public API, tests, package boundary,
manual, platform backend, and compatibility notes. It must build and install as
part of GameWIP and remain usable from a clean external CMake consumer through
an installed `GameWIP::` target.

Public headers expose portable types and must not require internal headers,
test hooks, game-runtime types, or accidental platform dependencies.

### Performance is part of API design

Reusable APIs preserve caller-owned storage where practical, avoid
success-path diagnostic allocation and redundant scans, and keep optional work
lazy. Benchmarks measure meaningful hot paths, but timing thresholds are not
correctness tests.

### Compatibility is explicit, not assumed

Installed packages require ABI-compatible C++23 toolchains, standard libraries,
runtimes, architectures, configurations, and matching GameWIP versions. The
project does not promise a universal stable C ABI.

Before 1.0, an incorrect public API may be fixed directly instead of retaining
a deprecated alias unless a real migration requirement justifies one. Umbrella
headers may remain supported while focused headers are introduced; splitting a
file does not by itself require a new namespace or package.

The detailed contracts live in @ref project_public_api_contract and
@ref project_library_compatibility.

## Validation and documentation

Correctness tests prove behavior, not elapsed time. Benchmarks measure
performance and registration health. Test modules use stable lowercase names
and join the shared runner; source-tree-only hooks are reserved for behavior
that cannot be made deterministic through the public API.

Manual checks remain opt-in so ordinary CI can run unattended. A workflow that
requires a person must say so explicitly.

Documentation is part of the supported surface. Header comments provide the
point-of-use contract, generated API pages expose declaration details, library
manuals explain concepts and composition, and the project manual explains
architecture, workflows, standards, and decisions. @ref project_documentation
defines what each layer must contain.

## Repository workflow and releases

Feature work normally moves from an issue to a short-lived branch, a pull
request with concrete validation evidence, and a squash merge. Titles use:

```text
area: imperative summary
```

The protected `master` checks are the pre-merge gate. Manual workflow runs are
for diagnostics and post-merge verification, not an alternate path around that
gate. @ref project_contributing explains day-to-day contribution flow, and
@ref project_repository_maintenance owns repository settings and check policy.

First-party source and documentation use the Apache License 2.0. Dependencies
and future non-code assets retain any separate licenses and notices that apply
to them. Contributions intentionally submitted for inclusion use Apache-2.0
unless the contributor and maintainer explicitly agree otherwise in writing.

The reviewed Git history is accepted for public visibility as project history,
but a newly discovered credential or sensitive artifact still requires
immediate rotation and, when necessary, history cleanup before further public
exposure.

## Updating these decisions

Add or change an entry only when the choice is durable and project-wide. State
the reason and the practical consequence, then update every manual or workflow
whose instructions changed. Use an issue for the implementation work rather
than embedding a checklist here.

Related detail is available in:

- @ref project_vision
- @ref project_roadmap
- @ref project_structure
- @ref project_cmake_infrastructure
- @ref project_extending
- @ref project_versioning
- @ref project_contributing
