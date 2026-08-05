@page project_roadmap Roadmap

The roadmap defines the completion gates for each GameWIP milestone. GitHub issues track implementation, validation, bugs, and follow-up work; the roadmap does not duplicate those tasks.

Early milestones have explicit checklists. Later milestones retain `TBD` entries until their design boundaries are known.

## Status legend

| Marker | Meaning |
| --- | --- |
| `[x]` | Complete or already established in the current repository baseline. |
| `[~]` | In progress or waiting for review. |
| `[!]` | Blocked. |
| `[ ]` | Required before the milestone is complete. |
| `[TBD]` | Required area exists, but the exact design or checklist is not known yet. |

## V1 target

V1 means a single-player sandbox experience with:

- Buildable vehicles and structures.
- Meaningful structural and component damage.
- Basic physics, collision, constraints, and vehicle behavior.
- Player interaction with creations.
- Weapons, projectiles, sensors, control logic, and guidance foundations.
- Save/load, content packaging, validation coverage, profiling coverage, and release documentation.

V1 does not require multiplayer, advanced world generation, full modding, or final art direction.

## Phase 0 — Foundation

### R00 — Bootstrap and reusable-library baseline

Status: `[x]` complete and published as `v0.0.1`.

Completion checklist:

- [x] Repository builds with CMake presets.
- [x] Windows MSYS2 UCRT64 development workflow exists.
- [x] MSYS2 CLANG64 AddressSanitizer workflow exists.
- [x] Root project version and generated runtime build identity exist.
- [x] Public-header, installed-consumer, and package-boundary checks exist for reusable libraries.
- [x] `foundation/io` exists with status/result and stream contracts.
- [x] `foundation/filesystem` exists with path, file, directory, metadata, and atomic-write helpers.
- [x] `foundation/terminal` exists with console IO and Win32 backend coverage.
- [x] `tools/logger` exists with async logging, sinks, filters, reports, macros, and validation hooks.
- [x] `tools/debug/assert` exists with assertion macros integrated with Logger.
- [x] `tools/test_support` exists with validation suites, reports, explicit infrastructure status/value results, non-throwing state guards, and child-process outcomes.
- [x] Modular correctness validation exists under `game/validation/tests`.
- [x] Benchmark registration exists under `game/validation/benchmarks`.
- [x] Static analysis, formatting, Doxygen, coverage, profiling, and repository-check workflows exist.
- [x] Generated developer documentation is grouped by reusable libraries, project manual pages, project contracts, quality workflows, and planning.
- [x] GitHub issues are the active task tracker; this roadmap is the milestone checklist.
- [x] The `v0.0.1` R00 tag and GitHub release are published.

## Phase 1 — Engine runtime and development visibility

### R01 — Window, input, and action foundation

Status: `[~]`

Purpose: establish the first engine-facing runtime loop and user input layer without committing to final rendering or gameplay systems.

Completion checklist:

- [x] `engine/window` has a documented public API and package boundary.
- [x] A native Win32 window can be created, shown, resized, focused, and closed.
- [x] Window lifecycle errors are reported through project status/result types.
- [x] Window event queues, high-DPI behavior, native renderer attachment, and renderer occlusion feedback boundaries are documented and validated.
- [ ] `engine/input` captures keyboard and mouse input from the active window.
- [ ] Input snapshots are stable across frame boundaries.
- [ ] `engine/action` maps raw input to named actions.
- [ ] Actions support press, release, held, and analog-style values where relevant.
- [ ] `engine/window_manager` composes window ownership and runtime polling.
- [ ] `GameWIP::Game::run()` enters a minimal runtime loop using the window/input/action path.
- [ ] The runtime loop can exit cleanly through window close and an explicit action.
- [ ] Logger and Assert integration exists for engine startup failures.
- [ ] Correctness tests cover action mapping, input-state transitions that can be tested deterministically, and lifecycle edge cases.
- [x] Manual validation exists for behavior that requires a real window.
- [ ] Every new engine library has public API documentation, a quick start, examples, validation guidance, troubleshooting guidance, and backend notes. Window meets this gate; the remaining R01 libraries do not yet.
- [ ] Package and installed-consumer validation cover every installable new engine library. Window meets this gate; the remaining R01 libraries do not yet.
- [TBD] WindowManager multi-window policy and Input controller support.

### R02 — Math foundation

Status: `[ ]`

Purpose: provide the math types and operations required by rendering, physics, transforms, camera work, debug drawing, and simulation validation.

Completion checklist:

- [ ] Math library ownership and namespace are defined.
- [ ] Scalar policy is selected for gameplay math.
- [ ] Vector types for 2D and 3D operations exist.
- [ ] Matrix types required for transforms and projection exist.
- [ ] Quaternion or rotation representation is selected and implemented.
- [ ] Transform composition, inversion, and interpolation exist.
- [ ] Common geometric primitives exist, such as rays, planes, boxes, and spheres.
- [ ] Bounding-volume helpers exist for early collision and debug visualization.
- [ ] Floating-point comparison helpers exist for validation.
- [ ] Unit, coordinate-system, handedness, and angle conventions are documented.
- [ ] Correctness tests cover representative vector, matrix, rotation, transform, and geometry operations.
- [ ] Public API docs, examples, testing docs, and troubleshooting notes exist.
- [ ] Package and installed-consumer validation include the math library when it becomes installable.
- [TBD] SIMD policy, exact type names, compile-time configuration, and serialization boundary.

### R03 — Minimal rendering and debug view

Status: `[ ]`

Purpose: make simulation and engine state visible enough to develop physics, collision, and gameplay systems.

Completion checklist:

- [ ] Rendering backend choice is made.
- [ ] A render surface can be attached to the R01 window path.
- [ ] Clear color and frame presentation work.
- [ ] Basic camera and transform path exists.
- [ ] Debug line or primitive drawing exists.
- [ ] Debug text or overlay information can be shown.
- [ ] Rendering failure and device-loss behavior are documented.
- [TBD] Backend API, shader asset layout, resource lifetime model, batching, and debug draw ownership.

### R04 — Simulation timing

Status: `[ ]`

Completion checklist:

- [ ] Frame timing exists.
- [ ] Fixed timestep accumulator exists.
- [ ] Update and render loops are separated.
- [ ] Previous and current simulation state can be stored for interpolation.
- [ ] Simulation behavior does not depend on render frame rate.
- [TBD] Final time-source abstraction, pause/slow-motion policy, and deterministic replay requirements.

### R05 — Basic audio foundation

Status: `[ ]`

Completion checklist:

- [ ] Audio backend choice is made.
- [ ] Audio device initializes and shuts down cleanly.
- [ ] Sound asset loading works for one supported format.
- [ ] One-shot sound playback works.
- [ ] Basic 3D positional sound works.
- [ ] Listener can follow the active camera.
- [ ] Audio can be disabled through configuration.
- [TBD] Mixer design, streaming policy, asset formats, and backend validation approach.

## Phase 2 — Physics core

### R06 — Rigid body physics core

Status: `[ ]`

Completion checklist:

- [ ] Rigid body representation exists.
- [ ] Mass, inverse mass, inertia, and transform state are represented.
- [ ] Force and torque accumulation are implemented.
- [ ] Linear and angular integration are implemented.
- [ ] Gravity and damping are implemented.
- [ ] Rotation normalization or drift correction is implemented.
- [ ] One-body falling and rotating scenarios are validated.
- [TBD] Integrator choice, units policy, sleeping threshold details, and debug visualization requirements.

### R07 — Collision detection

Status: `[ ]`

Completion checklist:

- [ ] Initial collider set is selected.
- [ ] Colliders can attach to bodies.
- [ ] Broadphase exists.
- [ ] Narrowphase detects ground and body contacts.
- [ ] Contact data includes normal, point, and penetration or depth information.
- [ ] Contacts can be debug drawn.
- [TBD] Exact collider list, broadphase structure, contact manifold policy, and continuous-collision requirements.

### R08 — Collision response

Status: `[ ]`

Completion checklist:

- [ ] Penetration correction exists.
- [ ] Impulse resolution exists.
- [ ] Restitution exists.
- [ ] Friction exists.
- [ ] Resting bodies are stable enough for simple stacks.
- [ ] Sleeping or low-activity handling exists.
- [TBD] Solver iteration policy, warm starting, stacking targets, and failure diagnostics.

### R09 — Constraints and joints

Status: `[ ]`

Completion checklist:

- [ ] Joint interface exists.
- [ ] Fixed joint works.
- [ ] Hinge joint works.
- [ ] Hinge limits work.
- [ ] Motorized hinge works.
- [ ] Assembly scenario validates connected bodies.
- [TBD] Solver coupling, breakable constraints, motor model, and debug visualization requirements.

## Later phases

Later milestones remain placeholders until earlier systems establish the required architecture. Create detailed GitHub issues when a milestone becomes active, then update this roadmap with concrete completion criteria.

| Milestone | Focus | Known completion gate |
| --- | --- | --- |
| R10 | Simulation framework | Entity/object lifetime, stable update order, debug inspection. |
| R11 | Structural world | Basic structural element representation and placement. |
| R12 | Structural connectivity | Connectivity graph, load paths, detach behavior. |
| R13 | Materials | Material properties affect strength, mass, and damage. |
| R14 | Structural destruction | Destruction affects geometry, connectivity, and behavior. |
| R15 | Building tools | Player-facing placement, removal, symmetry, and editing basics. |
| R16 | Component damage model | Components can degrade, fail, and expose functional state. |
| R17 | Component framework | Components attach to structures and communicate with simulation systems. |
| R18 | First components | Initial useful mechanical, electrical, or control components exist. |
| R19 | First vehicle | A player-built vehicle can be assembled, controlled, damaged, and tested. |
| R20-R22 | Player character and interaction | Character movement, interaction, and tool use exist. |
| R23-R29 | Weapons, projectiles, and damage fidelity | Weapons and damage systems interact with structures and components. |
| R30-R33 | Logic, control, sensors, and guidance | Controllable systems can read sensors and drive actuators. |
| R34-R37 | Building depth and mechanical systems | Building supports more complex mechanical and structural choices. |
| R38-R41 | Presentation, audio, and UX | The project becomes easier to play, inspect, and understand. |
| R42-R46 | World, persistence, content, gameplay loop, and AI targets | Saved worlds and basic game loops become coherent. |
| R47-R51 | Performance, stability, validation, content, and release candidate | V1 quality gates are prepared and validated. |

Each later milestone is `[TBD]` until its exact checklist is defined.

## V1 complete

### R52 — V1

Status: `[ ]`

Completion checklist:

- [ ] Core sandbox loop is playable from a clean build.
- [ ] Vehicle and structure building are functional.
- [ ] Destruction affects structure and components.
- [ ] Save/load and content workflow are defined.
- [ ] Validation, profiling, benchmark, and release workflows are complete.
- [ ] Public API, package, save-data, and compatibility promises are explicitly defined.
- [ ] `1.0.0` release preparation is complete.
- [TBD] Final V1 content scope, compatibility promise, and release checklist.

## Post-V1

Post-V1 work is intentionally outside the V1 release gate.

| Milestone | Focus | Notes |
| --- | --- | --- |
| PV1 | Multiplayer foundation | Networking model, authority, replication, and rollback or prediction policy are TBD. |
| PV2 | Advanced world and gameplay expansion | Larger worlds, AI expansion, campaign-like structure, modding depth, or advanced destruction are TBD. |

## Maintaining the roadmap

When updating the roadmap:

- Keep it as milestone completion criteria, not a duplicate issue list.
- Make early active milestones concrete.
- Use `TBD` for future details that are not designed yet.
- Move active implementation tasks, bugs, and follow-up cleanup to GitHub issues.
- Update @ref project_versioning when milestone numbering or release meaning changes.
- Update @ref project_decisions when roadmap changes reflect durable project direction.
