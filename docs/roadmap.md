@page project_roadmap Roadmap

## Purpose

This roadmap connects GameWIP's long-term vision to concrete release work. It
describes broad phases, ordered capability slices, and validation proofs.
GitHub issues remain the active tracker for implementation, bugs, validation,
and follow-up work.

The roadmap does not reserve release numbers for speculative work. It keeps
only understood near-term release gates concrete and leaves implementation
choices open until evidence makes them timely.

## Planning model

```text
long-term vision
    -> phases
    -> capability slices
    -> promoted release milestone Rxx
    -> GitHub issues
    -> implementation, tests, benchmarks, docs, and review
    -> release
    -> handoff or promotion of the next slice
```

A **phase** is a broad long-term capability and dependency area. It does not
reserve a release number and is not a strict waterfall.

A **capability slice** is an ordered outcome worth proving. It has no R number
or release version until it is understood well enough to become concrete
release work. Neighboring slices may overlap when real dependencies justify
it, but slices must not become a speculative issue ledger.

A **release milestone** is a concrete release gate. When a capability slice is
promoted, it receives the next sequential R number, concrete completion
criteria, and active GitHub implementation issues. The milestone number never
designs the game.

Phases and slices express dependency and focus direction, not a strict
waterfall. Rendering, audio, world, networking, and player systems evolve
vertically and continue changing after their first foundation slice.

## Status legend

| Marker | Meaning |
| --- | --- |
| `[x]` | Complete or already established in the current repository baseline. |
| `[~]` | In progress or waiting for review. |
| `[!]` | Blocked. |
| `[ ]` | Required before the concrete milestone is complete. |
| `[TBD]` | A near-term decision belongs in the milestone but is not settled yet. |

## Development principles and cross-cutting concerns

These concerns apply continuously rather than becoming late cleanup
milestones.

### Observability

Every major simulation capability should expose why it behaves as it does.
Inspection should cover physics contacts, forces, constraints, and sleep;
structural connections, loads, damage, and failure; networking authority,
replication, latency, and bandwidth; logic values and update rates; audio
emitters, voices, attenuation, and occlusion; and loaded world regions,
generation, and edits.

### Performance

There is no future "optimize everything" milestone. Each capability follows:

```text
design -> correctness -> representative workload -> measure -> optimize where needed
```

Architecture supports selective activity. Sleeping objects should cost almost
nothing, unchanged systems should avoid needless recomputation, irrelevant
distant entities should not replicate, inactive controllers should not run
expensive updates, and high-frequency control loops should run only where
required. GameWIP does not prematurely build for MMO-scale counts.

### Multiplayer awareness

Multiplayer is an early architectural constraint, not a post-V1 retrofit. Once
networking exists, later systems consider state ownership, authority, allowed
mutation and request boundaries, replication, relevance, and join-in-progress
behavior. Internal implementation state is not automatically network state.

### Audio and feedback

Audio exists from R05 onward and evolves continuously. Simulation produces
events and state; audio observes and translates them. Simulation must not
depend on audio existing.

### Identity, schemas, and persistence boundaries

Anything crossing a runtime, session, network, save, or content boundary gains
explicit identity and schema ownership when that boundary appears. Runtime
object, network, persistent creation or world, content, and configuration
identities remain distinct where their meanings differ. Configuration,
network, save, and asset schemas version independently from the GameWIP project
version.

Persistence evolves locally: input bindings and settings may persist early,
complex creation persistence appears around the first vehicle, world
persistence follows later, and authoritative server persistence is post-V1.
GameWIP does not create one universal ID, save schema, or persistence system up
front.

## Concrete numbered milestones R00-R05

These are the only numbered milestones currently reserved. No release number
is assigned after R05 until a capability slice is ready for promotion.

### R00 — Bootstrap and Reusable-Library Baseline

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
- [x] `tools/test_support` exists with validation suites, reports, explicit infrastructure status/value results, non-throwing state guards, and
  child-process outcomes.
- [x] Modular correctness validation exists under `game/validation/tests`.
- [x] Benchmark registration exists under `game/validation/benchmarks`.
- [x] Static analysis, formatting, Doxygen, coverage, profiling, and repository-check workflows exist.
- [x] Generated developer documentation is grouped by reusable libraries, project manual pages, project contracts, quality workflows, and planning.
- [x] GitHub issues are the active task tracker; this roadmap is the milestone checklist.
- [x] The `v0.0.1` R00 tag and GitHub release are published.

### R01 — Window, Input, and Action Foundation

Status: `[~]` active.

Purpose: establish the first engine-facing runtime loop and user input layer
without committing to final rendering or gameplay systems.

Completion checklist:

- [x] `engine/window` has a documented public API and package boundary.
- [x] A native Win32 window can be created, shown, resized, focused, and closed.
- [x] Window lifecycle errors are reported through project status/result types.
- [x] Window event queues, high-DPI behavior, native renderer attachment, and renderer occlusion feedback boundaries are documented and validated.
- [ ] `engine/input` captures keyboard and mouse input from the active window.
- [ ] Input snapshots are stable across frame boundaries.
- [ ] `engine/action` maps raw input to named actions.
- [ ] Actions support press, release, held, and analog-style values where relevant.
- [ ] User action bindings can be saved and loaded.
- [ ] Reusable Input and Action libraries expose the data needed to serialize and restore bindings without depending directly on FileSystem or a
  world-save system.
- [ ] Binding storage and composition live at the appropriate game or configuration layer.
- [ ] Any binding-data schema has its own compatibility version rather than using the GameWIP project version.
- [ ] `engine/window_manager` composes window ownership and runtime polling.
- [ ] `GameWIP::Game::run()` enters a minimal runtime loop using the window/input/action path.
- [ ] The runtime loop can exit cleanly through window close and an explicit action.
- [ ] Logger and Assert integration exists for engine startup failures.
- [ ] Correctness tests cover action mapping, input-state transitions that can be tested deterministically, and lifecycle edge cases.
- [x] Manual validation exists for behavior that requires a real window.
- [ ] Every new engine library has public API documentation, a quick start, examples, validation guidance, troubleshooting guidance, and backend
  notes. Window meets this gate; the remaining R01 libraries do not yet.
- [ ] Package and installed-consumer validation cover every installable new engine library. Window meets this gate; the remaining R01 libraries do not
  yet.
- [TBD] WindowManager multi-window policy and Input controller support.

Binding persistence in R01 is configuration persistence, not creation or world
persistence.

### R02 — Math Foundation

Status: `[ ]`

Purpose: provide only the math required by imminent rendering, physics, and
simulation work instead of building a speculative general framework.

Completion checklist:

- [ ] Required vector, matrix, rotation or quaternion, and transform operations exist.
- [ ] Rays, planes, bounds, and the required geometry helpers exist.
- [ ] Coordinate, unit, handedness, and angle conventions are documented.
- [ ] Required interpolation and floating-point validation helpers exist.
- [ ] Correctness tests cover representative operations used by imminent systems.
- [ ] Public API, examples, testing, troubleshooting, package, and installed-consumer boundaries are complete where applicable.
- [TBD] Exact types and policies are selected from imminent rendering, physics, and simulation requirements.

### R03 — Minimal Rendering and Debug Visualization

Status: `[ ]`

Purpose: make later simulation and physics observable without building the
final renderer.

Completion checklist:

- [ ] The renderer backend decision is made during this milestone.
- [ ] A render surface attaches to the R01 window path.
- [ ] Frame clear and presentation work.
- [ ] A basic camera and geometry path exist.
- [ ] Debug lines and shapes can be drawn.
- [ ] Debug text or overlay information can be shown.
- [ ] Rendering lifecycle, failure, and validation boundaries are documented.
- [TBD] Backend API, shader layout, and resource lifetime are selected from the proof needs.

### R04 — Simulation Timing

Status: `[ ]`

Completion checklist:

- [ ] Frame timing exists.
- [ ] A fixed-timestep accumulator exists.
- [ ] Render and simulation execution are separated.
- [ ] Previous and current simulation state have an interpolation boundary.
- [ ] Timing can be inspected and diagnosed.
- [ ] Pause and time-scaling foundations exist.
- [ ] Simulation behavior remains independent from render frame rate.
- [TBD] Exact time source and replay requirements are selected from demonstrated needs.

### R05 — Audio Foundation

Status: `[ ]`

Purpose: establish useful simulation-driven audio before physics and damage
become deep, without attempting advanced acoustics.

Completion checklist:

- [ ] The audio backend and device path are selected and initialize and shut down cleanly.
- [ ] Sound assets can be loaded and played.
- [ ] Voice and emitter lifetime is clean and inspectable.
- [ ] Listener and 3D emitter behavior exist with distance attenuation.
- [ ] Pitch, gain, looping, and basic buses or categories exist.
- [ ] Audio can be disabled and configured.
- [ ] Audio observes simulation state and events without simulation depending on audio.
- [ ] Correctness, manual validation, documentation, and failure diagnostics cover the supported path.
- [TBD] Asset formats, streaming, mixer depth, and advanced acoustics remain evidence-driven.

R01-R05 establish the runtime, visual and debug, timing, and audio supporting
spine before serious physics work begins.

## Capability roadmap

The following slices have no release numbers or release versions. Their order
expresses dependency and focus direction, not a strict waterfall.

### Playable Authoritative Simulation

#### Minimal Application / UI Shell

Make GameWIP game-shaped early. Prove launch, main menu with UI sound, settings
and audio settings, start a test sandbox, enter a 3D world, pause or return, and
quit cleanly. Include a developer overlay, but do not build the final UI.

#### Simulation Kernel / Simulation Spine

Establish authoritative world ownership, stable runtime object IDs, explicit
creation and destruction, stable update phases, commands and intents,
simulation events, observation and snapshot boundaries, headless operation,
and debug inspection. `world.step(fixedDt);` must make sense without rendering,
audio, human input, or transport. This slice neither requires nor rejects an
ECS.

#### Minimal World Foundation

Start with a finite test world, static ground, gravity and environment
parameters, entities, spatial queries, and simulation ownership. Separate local
simulation coordinates from eventual large persistent-world addressing. Do not
bake one origin-centered `float3` into every future API or choose final
large-world coordinate technology here.

#### Minimal Physics

Provide rigid-body transforms, mass and inertia, velocity, forces, torque,
gravity, and integration; colliders, broadphase, narrowphase, and contact data;
and impulses, penetration correction, friction, restitution, and basic sleep.
First prove that a cube falls, hits the ground, and responds correctly.

#### Integrated Physics / Rendering / Audio Proof

Prove that two colliding objects produce correct physical response, debug
visualization, useful contact, material, and energy information, and spatial
collision audio.

#### Network Foundation

Use server-authoritative gameplay simulation. Player intent reaches the same
command and authoritative-simulation boundary through a local or network
adapter. Single-player runs authority locally without UDP, serialization,
loopback traffic, or a fake client/server path. Multiplayer hosts authority on
a host or server.

Cover roles, local authority, headless servers, transport and sessions,
connection and player identity, network entity identity, ownership, messages,
serialization, commands, spawn and despawn, replication, join and leave,
initial synchronization, and diagnostics. Exclude server meshing, distributed
databases, MMO services, giant rollback systems, and sophisticated lag
compensation.

#### Minimal Player Presence

Before the first real two-client proof, add only player identity, placeholder
representation, basic movement, camera, interaction query, and selection. Full
character gameplay, inventory, animation, health, equipment, and FPS polish
remain later work.

#### First Authoritative Networked-World Proof

Start one server with two clients in the same tiny world. Keep the server
authoritative; replicate player and entity movement; show each client the
other; support spawn, despawn, join-in-progress, disconnect, and reconnect;
operate headlessly; and retain single-player without network traffic.

### Shared Engineering Construction

#### Physics Depth

Add only constraints and stability needed by real assemblies: fixed joints,
hinges, limits, motors, improved contacts and sleeping, justified CCD, and
debug tools. Do not implement every imaginable joint.

#### Structural Foundation

Represent structural elements, connections, transforms, mass contribution,
material identity, and structural state. Vehicles and buildings share this
foundation where concepts genuinely match. Do not begin with full finite-
element analysis.

#### Networked Building Proof

Prove player A requests placement, the server validates and changes the
structure, and A and B see the same result. Cover removal, invalid and
simultaneous requests, join-in-progress, stable structure IDs, and basic
ownership and permissions.

#### Building Tools

Add placement preview, snapping, rotation, removal, selection, symmetry,
editing, and inspection. Copy and undo arrive later only if naturally
supported.

#### Structural Damage

Carry impact through damage, element or connection changes, strength or
connectivity changes, detachment or failure, and resulting physics. Share
density, strength, damage traits, and surface or acoustic identity where useful
without forcing unrelated systems into one material type. Make collision audio
material-aware.

#### Component Foundation

Attach components to structures with stable identity, state, activation,
failure, inputs and outputs, replication, and inspection. Introduce electrical,
mechanical, fluid, or control and data domains only when real components need
them, not as one universal resource network.

#### First Useful Components

Implement only enough useful components for the first real vehicle and machine
proof.

#### First Vehicle

Prove build, control, movement, collision, sound, damage, detachment or
component failure, and the same authoritative result for another player.

#### Creation Persistence

Prove stable serialization identity, structural elements and connections,
component identity and state, an independent schema version, save, load, and
reconstruction. This is not GameWIP's first persistence. Blueprint sharing may
come later; save, network, asset, and configuration schemas remain independent
from the project version.

#### First Mounted Weapon / Projectile Proof

Add generic trigger, firing state, cooldown, optional ammo or resource use,
muzzle or origin, and damage source plus projectile spawn, movement, lifetime,
collision, and damage. Prove a server-validated control fires, collides, causes
structural damage, replicates, and produces audio. Terrain damage waits for
modifiable terrain.

### World and Machine Depth

#### World Generation Foundation

Replace the test world with spatial regions, chunks, cells, or sectors; the
name remains open. Base generation uses world seed, generation version, and
region coordinate. This does not imply deterministic whole-game physics,
lockstep networking, or cross-platform bit-identical rigid bodies. Cover
generation, rendering, collision, spawn rules, material distribution,
debugging, and justified async work without choosing final terrain technology.

#### World Streaming

Separate generation from lifecycle. Nearby regions are active and detailed,
far regions use lower detail or metadata, and irrelevant regions unload. Cover
safe request, load, and unload; async work; render, collider, and entity
lifecycle; LOD and relevance; eventual multiple viewers; and diagnostics. Do
not add a generic job system without evidence from multiple real systems.

#### World Modification / Destruction

Promise modifiable terrain while keeping representation open. First settle
product needs for tunnels, caves, overhangs, mining, player-added terrain,
craters, resolution, and expected modified extent. Do not select heightfield,
voxel, SDF, hybrid, or mesh technology in this roadmap.

#### World Persistence

Treat the current world as reproducible base regions plus persistent
modifications plus persistent entities and creations. Cover local saves,
terrain edits, creations, entities, generation and save schema versions,
migration, and safe async IO. Regenerate untouched regions where practical
rather than saving every generated primitive.

#### Machine-System Depth

Expand engines, motors, transmissions, wheels, thrusters, actuators, batteries,
generation, power, pumps, fluids, fuel, and thermal behavior from real vehicle
needs. Build a small useful version, integrate it, then deepen it.

#### Logic, Control, and Sensors

Build `sensor -> signal -> logic/controller -> actuator` with inputs, outputs,
logic, sensors, actuators, automation, and inspection. Add stabilization,
navigation, tracking, autopilot, and guidance only when justified, without
running the whole simulation at control-loop frequency.

#### Damage Depth

Deepen loading, connection failure, penetration, armor, deformation,
components, heat, fire, explosions, fragmentation, materials, terrain response,
and cascading failure only when vehicles, weapons, and world make them useful.

### Product Depth Toward V1

#### Proper Player Systems

Expand presence into movement, cameras, interaction, inventory, equipment,
tools, seats, vehicle use, repair, and appropriate health or damage. The player
remains part of the same simulation.

#### Player-Held Tools and Weapons

Reuse the generic weapon, projectile, and damage foundation. Add equipping,
representation, aiming, reload, recoil, animation, presentation, and justified
prediction or lag compensation without creating a player-only weapon system.

#### Advanced Audio / Acoustics

Deepen R05 with material impacts, machinery state, interiors and exteriors,
occlusion, obstruction, transmission, reverb, environmental acoustics,
filtering, Doppler, destruction sound, and useful reflection or zones. Acoustic
geometry need not equal render geometry; target realistic-feeling sound, not
expensive mathematics for its own sake.

#### Environment Simulation

Add justified day and night, weather, wind, atmosphere, temperature, water,
buoyancy, waves, hydrodynamics, and hazards integrated with vehicles, physics,
world, audio, sensors, and damage.

#### Rendering Depth

Continue through materials, lighting, shadows, terrain and large-object
rendering, LOD, instancing, particles, destruction, weather, post-processing,
visibility, scalable rendering, UI, and GPU profiling. Simulation decides what
happened; rendering decides how it looks.

#### Gameplay Sandbox / Content

Develop world-start flow, useful resources, exploration, repair and logistics,
engineering challenges, objectives, optional progression, AI, content
definitions, and creation sharing in service of creative sandbox play.

#### Multiplayer Productization

Polish host and join UI, dedicated-server workflow, reconnect, appropriate
discovery, permissions, server configuration, prediction and interpolation,
security, bandwidth measurement, tooling, and interest management. Nearby
active creations receive detail; far or sleeping ones receive coarse or almost
no state; irrelevant world areas receive none.

## Vertical proof ladder

1. Open a window and receive input.
2. See a 3D world or debug view.
3. Use the application shell and hear feedback.
4. Run a headless-capable authoritative simulation.
5. Drop objects and collide them.
6. Hear collisions spatially and inspect why they happened.
7. Control a minimal player presence.
8. Connect two clients to one authoritative world.
9. Build the same structure together.
10. Damage it and detach pieces.
11. Add working components.
12. Build and drive the first vehicle together.
13. Save and reload that creation.
14. Fire a mounted weapon or projectile and damage the shared structure.
15. Drive through a procedurally generated world.
16. Stream the world.
17. Modify or destroy terrain.
18. Reload the world and see modifications persist.
19. Build deeper machines and automated or control-driven systems.
20. Deepen weapons and damage and add proper player tools.
21. Experience richer environment, audio, acoustics, and rendering.
22. Play a polished engineering sandbox alone or with friends.
23. V1.
24. Keep the multiplayer world running persistently.
25. Scale world, player, and creation counts from measurements.
26. Introduce distributed or multi-server architecture only when measured limits require it.

If a proposed major subsystem does not advance an imminent proof or enable one,
question whether it belongs yet.

## V1 direction

V1 has no predetermined R number. When its capability slice is understood and
promoted, it receives the next sequential release milestone like any other
concrete work.

V1 is a strong engineering sandbox that works very well alone and with a small
group of friends. It requires a polished application shell; good building;
vehicles and structures; structural and component damage; a generated and
intentionally modifiable world; creation and world persistence; useful machine,
logic, control, sensor, weapon, projectile, player, tool, environment, audio,
and content systems; observability; profiling and benchmarks; measured
performance; release-quality UX and docs; small-group server-authoritative
multiplayer; host and join flow; join-in-progress and reconnect; supported
headless or dedicated servers; and explicit public API, package, save, and
compatibility commitments.

V1 does not require an always-running persistent universe, MMO-scale
population, server meshing, sharding, distributed databases, multi-server
handoff, cloud account infrastructure, or giant rollback or lockstep systems.

## Post-V1 direction

### Persistent Multiplayer

Move from friends playing a session to a world independent of connected
players. Add server-owned persistence, persistent players, creations, and
terrain edits, ownership, reconnect, server lifecycle, backups and recovery,
and explicit storage or database boundaries.

### Large-World Scaling

Measure real workloads. Develop selective simulation, region scheduling,
streaming, async persistence, spatial indexing, creation LOD, physics
activation, interest management, update priorities, bandwidth budgets,
compressed replication, aggregation, load tests, memory budgets, persistence
throughput, and simulation budgets from evidence.

### Distributed / MMO-Scale Architecture

Only after measured single-server or process limits justify it, evaluate
multiple simulation owners, regional handoff, server meshing, shards,
persistence services, account services, load balancing, and distributed
persistence. These are future questions, not current decisions.

## Maintaining the roadmap

- Keep only active and understood near-term release milestones numbered and concrete.
- Promote a slice only when it can own meaningful completion criteria and active issues.
- Assign the next sequential R number at promotion; never reserve numbers for unpromoted slices.
- Keep slices as outcomes, proofs, and constraints, not speculative checklists or issue ledgers.
- Create detailed GitHub issues only for active concrete milestone work.
- Update @ref project_versioning when release meaning changes.
- Update @ref project_decisions when roadmap changes reflect durable project direction.

Locked architecture includes headless authoritative simulation, fixed timing,
command and state boundaries across authority, local authority without transport
for single-player, server authority for multiplayer, boundary-specific identity
and schema versions, partitionable worlds with reproducible base regions,
selective activity and relevance, presentation-independent simulation,
simulation-driven audio, replaceable large-scale hosting, and evidence-driven
performance.

Deferred choices include entity model, physics library, renderer and audio
backends, network transport, serialization format, storage technology, region
layout, terrain representation, generic scheduling, prediction or rollback,
and distributed hosting topology.
