# GameWIP Roadmap

## Purpose

This file tracks the planned development order for GameWIP.

The roadmap is an ordered list of major milestones. It describes what should be built next and what each milestone needs before it can be considered complete.

The goal of this roadmap is to reach a complete single-player V1: a playable sandbox where the player can build vehicles and structures, damage and destroy them, use components and weapons, move as a character, customize infantry weapons, and interact with a world that feels coherent.

This file is not the implementation proof checklist and not the testing proof checklist.

- Use `implementation_checklist.md` to track what has already been implemented.
- Use `testing_checklist.md` to track what must be proven by tests.
- Use `decisions.md` for stable architecture and project decisions.
- Use `vision.md` for the high-level identity of the game.

---

## V1 definition

GameWIP V1 means the game has a complete playable foundation.

V1 should include:

- Stable single-player sandbox gameplay
- Usable vehicle and structure building
- Destructible structures
- Meaningful component damage
- Simple but functional components
- At least one controllable vehicle type
- Player character movement and interaction
- Infantry gunplay with meaningful weapon customization
- Projectiles, explosives, armor, and penetration basics
- Logic and control systems for creations
- Sensors and actuators
- Basic world and map support
- Save and load support for creations and worlds
- Audio feedback for important gameplay events
- Graphics that support a readable Stormworks/Starbase-like style
- Usable UI and UX for building, configuration, inventory, and debugging
- Acceptable performance for medium-scale creations and combat scenarios
- Enough content to prove the sandbox loop

V1 does not require multiplayer. Multiplayer is a post-V1 milestone unless the project direction changes.

---

## Status legend

```text
[ ] Not started
[-] In progress
[x] Done
[~] Partially done / needs cleanup
[!] Blocked or needs a decision
```

---

## Phase 0 — Foundation Already Started

This phase contains the basic project foundation needed before real engine/game work can move quickly.

### R00 — Bootstrap

Status: `[-]`

- [x] Project builds from CMake.
- [x] Executable runs.
- [x] Folder structure exists.
- [x] Logging system exists.
- [x] Assertions/debug checks exist.
- [x] Automated validation and documentation workflows exist.
- [ ] Final R00 validation and release/version rules are complete.

Completion goal:

The project can configure, build, run, test, document, package, log, and assert on the supported development environment.

---

## Phase 1 — Engine Runtime and Development Visibility

This phase establishes the runtime boundaries needed to receive input, represent engine state, display and hear results, and update simulation predictably.

### R01 — Window, Input, and Action Foundation

Status: `[ ]`

- [ ] A platform-neutral window API and Win32 backend exist.
- [ ] A window opens, remains responsive, and closes cleanly through both application and operating-system requests.
- [ ] Resize, move, focus, minimize, restore, and close events have explicit behavior.
- [ ] Keyboard and mouse state expose current, pressed, and released transitions without losing events between frames.
- [ ] Text input is distinct from physical key input.
- [ ] Absolute and relative mouse modes, cursor visibility, and cursor capture restore operating-system state correctly.
- [ ] Named digital actions and analog axes can bind to physical inputs without platform input leaking into gameplay code.
- [ ] The event pump, window lifetime, input state, and action mapping have deterministic automated coverage and focused internal test hooks where native behavior cannot be synthesized publicly.
- [ ] The executable demonstrates the window and input/action loop without depending on a renderer.

Completion goal:

The engine can own a responsive native window and expose predictable, platform-independent input and named actions to later camera, UI, and gameplay systems.

---

### R02 — Math Foundation

Status: `[ ]`

- [ ] `Vec3` implemented.
- [ ] Basic operations implemented: `+`, `-`, `*`, `/`.
- [ ] Dot product implemented.
- [ ] Cross product implemented.
- [ ] Normalize and length implemented.
- [ ] `Mat4` basic functionality implemented.
- [ ] Quaternion basic functionality implemented.
- [ ] Transform helpers implemented.
- [ ] Geometry helpers implemented: AABB, ray, plane.
- [ ] Math tests pass.

Completion goal:

The project has a reliable math layer for transforms, physics, collision, rendering, and gameplay systems.

---

### R03 — Minimal Rendering and Debug View

Status: `[ ]`

- [ ] A rendering backend, graphics device, and presentation surface initialize and shut down cleanly.
- [ ] The renderer can clear and present the R01 window and recover correctly from resize/minimize events.
- [ ] One transformed object renders through a minimal shader and pipeline.
- [ ] A free debug camera uses R01 actions and R02 math.
- [ ] Transform handling is sufficient for object and camera placement.
- [ ] Debug lines and basic shapes can be drawn.
- [ ] On-screen debug information exists.
- [ ] Debug overlays can be toggled through named actions.

Completion goal:

The engine can show simple objects and debug information well enough to inspect later simulation behavior.

---

### R04 — Simulation Timing

Status: `[ ]`

- [ ] Frame timing implemented.
- [ ] Fixed timestep accumulator implemented.
- [ ] Render and update loops separated.
- [ ] Previous and current simulation state stored.
- [ ] Render interpolation supported.
- [ ] Same simulation behavior at different render FPS verified.

Completion goal:

Simulation runs consistently enough that rendering FPS does not change physics behavior.

---

### R05 — Basic Audio Foundation

Status: `[ ]`

- [ ] Audio backend selected.
- [ ] Audio device initializes and shuts down cleanly.
- [ ] Sound asset loading works.
- [ ] One-shot sound playback works.
- [ ] Basic 3D positional sound works.
- [ ] Listener follows the active camera.
- [ ] Simple sound events can be emitted from simulation/debug code.
- [ ] Audio can be disabled through configuration.

Completion goal:

The engine can play simple sounds and attach them to world events without affecting simulation behavior.

---

## Phase 2 — Physics Core

This phase creates the first physical simulation layer.

### R06 — Rigid Body Physics Core

Status: `[ ]`

- [ ] Rigid body struct created.
- [ ] Force accumulation implemented.
- [ ] Torque accumulation implemented.
- [ ] Linear integration implemented.
- [ ] Angular integration implemented.
- [ ] Gravity applied.
- [ ] One-body falling test works.
- [ ] Damping added.
- [ ] Rotation normalization implemented.

Completion goal:

A single rigid body can move, rotate, fall, and remain numerically stable.

---

### R07 — Collision Detection

Status: `[ ]`

- [ ] Collider types implemented: plane and box.
- [ ] Colliders can be attached to bodies.
- [ ] Basic broadphase exists.
- [ ] Body-ground collision detected.
- [ ] Body-body collision detected.
- [ ] Contact data generated: normal and point.
- [ ] Contacts can be debug drawn.

Completion goal:

The engine can detect simple collisions and expose contact information for debugging and response.

---

### R08 — Collision Response

Status: `[ ]`

- [ ] Penetration correction implemented.
- [ ] Impulse resolution implemented.
- [ ] Restitution implemented.
- [ ] Friction implemented.
- [ ] Stable resting bodies supported.
- [ ] Multiple-body collision stability tested.
- [ ] Sleeping system implemented.

Completion goal:

Bodies collide, rest, stack, and stop moving when appropriate without obvious instability.

---

### R09 — Constraints and Joints

Status: `[ ]`

- [ ] Joint interface exists.
- [ ] Fixed joint works.
- [ ] Bodies attach correctly.
- [ ] Hinge joint works.
- [ ] Hinge limits implemented.
- [ ] Motorized hinge implemented.
- [ ] Assembly test works.

Completion goal:

The physics system can support connected mechanical assemblies.

---

## Phase 3 — Simulation Framework and Structures

This phase turns isolated physics into a world that can hold objects, structures, and destructible cells.

### R10 — Simulation Framework

Status: `[ ]`

- [ ] World container exists.
- [ ] Entity IDs are stable.
- [ ] Update phases exist.
- [ ] Object lifetime management exists.
- [ ] Event system or event queue exists.
- [ ] Profiling markers added.
- [ ] Serialization stub exists.

Completion goal:

The engine has a clear world/update structure that future systems can plug into.

---

### R11 — Structural World

Status: `[ ]`

- [ ] Chunk system created.
- [ ] Cell addressing works.
- [ ] Chunk storage exists.
- [ ] Cell data defined.
- [ ] Cells render as cubes.
- [ ] Raycast for building works.
- [ ] Place cell works.
- [ ] Remove cell works.
- [ ] Save/load structures.

Completion goal:

The player or debug tools can place, remove, display, and save simple structural cells.

---

### R12 — Structural Connectivity

Status: `[ ]`

- [ ] Neighbor lookup works.
- [ ] Connected regions detected.
- [ ] Structure IDs assigned.
- [ ] Connectivity recomputes after edits.
- [ ] Structure split detection works.
- [ ] Detached structure becomes a rigid body.
- [ ] Falling structure test works.

Completion goal:

Structures can split, detach, and become physical bodies when support is lost.

---

### R13 — Materials

Status: `[ ]`

- [ ] Material database created.
- [ ] Density defined.
- [ ] HP defined.
- [ ] Structural strength defined.
- [ ] Mass uses density.
- [ ] Damage uses HP.
- [ ] Support uses strength.
- [ ] Materials can influence impact/damage feedback.

Completion goal:

Cells and structures have material properties that affect mass, damage, support, and feedback.

---

### R14 — Structural Destruction

Status: `[ ]`

- [ ] Damage can be applied to cells.
- [ ] HP is reduced correctly.
- [ ] Cells are destroyed at zero HP.
- [ ] Connectivity recomputes after damage.
- [ ] Explosion damage implemented.
- [ ] Damage visualization exists.
- [ ] Collapse test works.
- [ ] Destruction emits useful events for sound, particles, and gameplay feedback.

Completion goal:

Damage can destroy parts of a structure and cause meaningful physical consequences.

---

## Phase 4 — Building and Components

This phase starts turning structures into useful creations.

### R15 — Building Tools

Status: `[ ]`

- [ ] Rotation placement.
- [ ] Drag-line placement.
- [ ] Drag-plane placement.
- [ ] Mirror mode.
- [ ] Undo/redo.
- [ ] Placement preview improved.
- [ ] Basic part selection UI exists.

Completion goal:

Building becomes usable enough for creating simple test structures and vehicles.

---

### R16 — Component Damage Model

Status: `[ ]`

- [ ] Component base structure defined.
- [ ] Damage states defined.
- [ ] Exposure rules defined.
- [ ] Damage sources defined.
- [ ] Detach rules defined.
- [ ] Test component attached.
- [ ] Component damage/detach works.

Completion goal:

Components can be damaged, degraded, exposed, detached, or destroyed in a consistent way.

---

### R17 — Component Framework

Status: `[ ]`

- [ ] Component registry exists.
- [ ] Mount points implemented.
- [ ] Ports implemented: power and data.
- [ ] Component update interface exists.
- [ ] Mass/inertia contribution added.
- [ ] Component save/load implemented.
- [ ] Component debug inspection exists.

Completion goal:

Components can be registered, mounted, connected, updated, saved, loaded, and inspected.

---

### R18 — First Components

Status: `[ ]`

- [ ] Battery works.
- [ ] Thruster works.
- [ ] Fuel tank works.
- [ ] Controller works.
- [ ] Components mount on structure.
- [ ] Thruster affects body.
- [ ] Simple vehicle works.

Completion goal:

The first functional vehicle can be built from simple components.

---

### R19 — First Vehicle

Status: `[ ]`

- [ ] Total mass aggregation.
- [ ] Center of mass computed.
- [ ] Player input mapped.
- [ ] Small craft built.
- [ ] Craft controllable.
- [ ] Save/load craft.
- [ ] Vehicle can be damaged and still behave coherently.

Completion goal:

A player-controllable saved vehicle exists and behaves according to mass, thrust, structure, and damage.

---

## Phase 5 — Player Character and Interaction

This phase makes the player a real gameplay entity, not only a camera or vehicle controller.

### R20 — Player Character Foundation

Status: `[ ]`

- [ ] Player entity exists.
- [ ] First-person camera works.
- [ ] Basic movement works.
- [ ] Jump/crouch or stance system exists.
- [ ] Player collision works.
- [ ] Player can interact with vehicles and structures.
- [ ] Player can enter and exit vehicles.
- [ ] Player can place/remove/interact with build parts.
- [ ] Basic inventory or held-item system exists.

Completion goal:

The player can move through the world, interact with creations, and act as a real gameplay entity.

---

### R21 — Player Interaction and Tools

Status: `[ ]`

- [ ] Interaction raycast works.
- [ ] Use/interact action works.
- [ ] Repair or maintenance tool prototype exists.
- [ ] Build tool can be used by the player.
- [ ] Component configuration can be opened from the world.
- [ ] Player can inspect damaged parts.
- [ ] Player can interact with doors, seats, panels, and controls.

Completion goal:

The player can directly interact with vehicles, structures, components, and building systems.

---

### R22 — Player Inventory and Equipment

Status: `[ ]`

- [ ] Inventory data model exists.
- [ ] Items can be picked up and dropped.
- [ ] Held items work.
- [ ] Equipment slots exist.
- [ ] Weapons/tools can be equipped.
- [ ] Ammo or resource items can be represented.
- [ ] Inventory save/load works.

Completion goal:

The player has enough inventory/equipment support for tools, weapons, ammo, and gameplay items.

---

## Phase 6 — Weapons, Projectiles, and Gunplay

This phase adds infantry weapons, vehicle weapons, projectiles, damage, and customization.

### R23 — Projectile and Damage Foundation

Status: `[ ]`

- [ ] Projectile entity exists.
- [ ] Projectile motion simulated.
- [ ] Structure hit detection works.
- [ ] Structural damage applied.
- [ ] Component hit detection works.
- [ ] Component damage applied.
- [ ] Support-loss detach works.
- [ ] Projectile debug visualization exists.

Completion goal:

Projectiles can hit structures and components, apply damage, and interact with the destruction system.

---

### R24 — Infantry Gunplay Foundation

Status: `[ ]`

- [ ] Player-held weapon system exists.
- [ ] Weapon firing works.
- [ ] Weapon aiming works.
- [ ] Reloading works.
- [ ] Recoil behavior exists.
- [ ] Spread/accuracy behavior exists.
- [ ] Projectile or hitscan decision is made per weapon type.
- [ ] Weapon animations or placeholder feedback exists.
- [ ] Weapon sounds can be triggered through audio events.

Completion goal:

The player can use basic infantry weapons with readable firing, aiming, recoil, and reload behavior.

---

### R25 — Weapon Customization

Status: `[ ]`

- [ ] Weapon attachment system exists.
- [ ] Attachments can affect weapon behavior.
- [ ] Sights/optics can be mounted.
- [ ] Barrels can be changed.
- [ ] Stocks can be changed.
- [ ] Magazines can be changed.
- [ ] Grips can be changed.
- [ ] Muzzle devices can be changed.
- [ ] Ammo types can affect damage/penetration behavior.
- [ ] Weapon UI shows important stats clearly.
- [ ] Basic weapon customization screen exists.

Completion goal:

Infantry weapons support meaningful customization depth while staying readable and usable.

---

### R26 — Vehicle Weapons

Status: `[ ]`

- [ ] Mounted weapon component exists.
- [ ] Weapon can be attached to a structure.
- [ ] Weapon can be controlled by player input.
- [ ] Weapon can be controlled by logic.
- [ ] Ammo or power dependency exists.
- [ ] Recoil affects the structure or vehicle where appropriate.
- [ ] Weapon damage affects structures and components.

Completion goal:

Vehicles and structures can mount functional weapons that interact with the same damage systems as infantry weapons.

---

### R27 — Explosives and Area Damage

Status: `[ ]`

- [ ] Explosion event exists.
- [ ] Blast radius damage implemented.
- [ ] Damage falloff implemented.
- [ ] Structural blast damage works.
- [ ] Component blast damage works.
- [ ] Detached parts can result from explosions.
- [ ] Explosion visual and audio events are emitted.

Completion goal:

Explosions can damage structures, components, and vehicles in a physically meaningful way.

---

### R28 — Armor and Penetration

Status: `[ ]`

- [ ] Armor thickness logic implemented.
- [ ] Material penetration logic implemented.
- [ ] Residual damage implemented.
- [ ] Layered armor implemented.
- [ ] Angle or impact direction can matter.
- [ ] Ammo types interact with armor differently.
- [ ] Armor layouts matter.

Completion goal:

Armor is not only extra HP. Material, thickness, layering, layout, and ammo type affect damage results.

---

### R29 — Damage Fidelity

Status: `[ ]`

- [ ] Partial damage works.
- [ ] Internal exposure works.
- [ ] Degraded behavior works.
- [ ] Component failure effects work.
- [ ] Blast behavior improved.
- [ ] Layered damage verified.
- [ ] Damage states are readable to the player.

Completion goal:

Damage has enough depth that internal layout, exposure, and degraded parts matter.

---

## Phase 7 — Logic, Control, and Sensors

This phase adds programmable/control behavior.

### R30 — Logic System

Status: `[ ]`

- [ ] Signal types defined.
- [ ] Logic node interface exists.
- [ ] Connection graph exists.
- [ ] Basic nodes implemented: AND, OR, NOT, etc.
- [ ] Fixed logic tick implemented.
- [ ] Logic controls components.
- [ ] Player logic works.
- [ ] Logic state can be inspected/debugged.

Completion goal:

Simple logic networks can control components in a predictable fixed-tick system.

---

### R31 — Multi-Rate Simulation

Status: `[ ]`

- [ ] Logic tick separate from physics.
- [ ] Read rules defined.
- [ ] Write rules defined.
- [ ] Higher-rate logic tested.
- [ ] Stability verified.
- [ ] Selective high-rate support implemented.
- [ ] Debug tools show which systems run at which rate.

Completion goal:

Only selected systems can run at higher rates without destabilizing the rest of the simulation.

---

### R32 — Sensors and Actuators

Status: `[ ]`

- [ ] Sensors implemented.
- [ ] Actuators implemented.
- [ ] Stabilizer craft built.
- [ ] Closed loop works.
- [ ] Control tuned.
- [ ] Sensor data can feed logic.
- [ ] Actuators can affect components and mechanical systems.

Completion goal:

Feedback-control systems can sense state, act on components, and stabilize a craft.

---

### R33 — Guidance and Targeting Prototype

Status: `[ ]`

- [ ] Basic target data model exists.
- [ ] Simple aiming assist or targeting computer works.
- [ ] Basic missile/rocket guidance prototype exists.
- [ ] Sensor-to-control loop works.
- [ ] Weapon system can use sensor data.
- [ ] Debug visualization shows target/guidance data.

Completion goal:

The game proves that sensors, logic, control, and weapons can work together for guided systems.

---

## Phase 8 — Building Depth and Mechanical Systems

This phase improves construction depth without sacrificing usability.

### R34 — Building Representation

Status: `[ ]`

- [ ] Larger build parts exist.
- [ ] Parts map to internal cells.
- [ ] Internal destruction is granular.
- [ ] Easy build plus granular destruction works.
- [ ] Visual representation and internal damage representation stay synchronized.

Completion goal:

Players can build with convenient larger parts while the simulation still supports internal granular damage.

---

### R35 — Component Tiers

Status: `[ ]`

- [ ] Simple mode works.
- [ ] Configurable mode works.
- [ ] Advanced mode exists.
- [ ] Advanced mode hidden by default.
- [ ] Beginner usability preserved.
- [ ] Advanced stats are understandable when opened.

Completion goal:

Components support optional depth without making basic building harder.

---

### R36 — Advanced Component Family

Status: `[ ]`

- [ ] One component family selected.
- [ ] Simple version works.
- [ ] Configurable version works.
- [ ] Advanced version works.
- [ ] Depth vs ease validated.
- [ ] UI proves the advanced model is usable.

Completion goal:

One component family proves the simple/configurable/advanced model before expanding it to more systems.

---

### R37 — Mechanical Assemblies

Status: `[ ]`

- [ ] Hinges buildable.
- [ ] Limits and motors usable.
- [ ] Turret built.
- [ ] Elevation works.
- [ ] Weapon mounted.
- [ ] Controlled by player or logic.
- [ ] Mechanical stress or damage interaction considered.

Completion goal:

Mechanical assemblies can be built, controlled, and used for functional systems such as turrets.

---

## Phase 9 — Presentation, Audio, and UX

This phase turns the technical prototype into something readable and enjoyable.

### R38 — Gameplay Audio

Status: `[ ]`

- [ ] Collision impact sounds use impact strength.
- [ ] Material-based impact sounds exist.
- [ ] Thruster loop sounds work.
- [ ] Motor and hinge loop sounds work.
- [ ] Weapon fire sounds work.
- [ ] Projectile impact sounds work.
- [ ] Explosion sounds work.
- [ ] Structural damage and breaking sounds work.
- [ ] Component failure sounds work.
- [ ] Warning, alarm, and sensor tones work.
- [ ] Basic audio mixing and volume categories exist.

Completion goal:

Major gameplay systems produce useful sound feedback, making vehicles, weapons, damage, and components feel alive and readable.

---

### R39 — Advanced Graphics and Visual Presentation

Status: `[ ]`

- [ ] Stable render pipeline exists.
- [ ] Basic lighting works.
- [ ] Material rendering works.
- [ ] Shadows or simple shadow alternative exists.
- [ ] Part/structure visuals are readable.
- [ ] Damage visuals are readable.
- [ ] Weapon/projectile/explosion effects exist.
- [ ] Thruster and engine effects exist.
- [ ] UI and world visuals share a consistent style.
- [ ] Visual style target is closer to Stormworks/Starbase than photorealism.

Completion goal:

The game has a readable and consistent visual identity that supports building, combat, destruction, and exploration.

---

### R40 — UI Foundation

Status: `[ ]`

- [ ] Main menu exists.
- [ ] Settings menu exists.
- [ ] Pause menu exists.
- [ ] Basic HUD exists.
- [ ] Interaction prompts exist.
- [ ] Vehicle HUD exists.
- [ ] Weapon HUD exists.
- [ ] Debug UI can be toggled separately from player UI.

Completion goal:

The player has the basic UI needed to launch, configure, play, pause, interact, drive, and fight.

---

### R41 — Building and Configuration UX

Status: `[ ]`

- [ ] Part browser improved.
- [ ] Search/filter/categories exist.
- [ ] Config panel improved.
- [ ] Measurement tools exist.
- [ ] Center-of-mass and force overlays exist.
- [ ] Connection overlays exist.
- [ ] Component configuration UI is usable.
- [ ] Weapon customization UI is usable.
- [ ] Blueprint system exists.
- [ ] Selection tools improved.

Completion goal:

Building, configuring, debugging, saving, and modifying creations becomes easier and less frustrating.

---

## Phase 10 — World, Content, and Gameplay Loop

This phase turns systems into a real playable sandbox.

### R42 — World and Map

Status: `[ ]`

- [ ] Terrain chunks exist.
- [ ] Terrain rendering works.
- [ ] Destructible terrain prototype exists.
- [ ] Static structures exist.
- [ ] World streaming exists.
- [ ] Large world test works.
- [ ] Spawn locations or test zones exist.

Completion goal:

The game supports larger environments with terrain, structures, and streaming.

---

### R43 — Save/Load and Persistence

Status: `[ ]`

- [ ] Save/load world works.
- [ ] Save/load structures works.
- [ ] Save/load vehicles works.
- [ ] Save/load player inventory works.
- [ ] Save/load component states works.
- [ ] Save/load damage states works.
- [ ] Save format versioning considered.
- [ ] Corrupt or missing save handling exists.

Completion goal:

The player can keep worlds, vehicles, structures, inventory, and damage states across sessions.

---

### R44 — Content Expansion

Status: `[ ]`

- [ ] More propulsion types.
- [ ] More power components.
- [ ] More sensors.
- [ ] More utility parts.
- [ ] More weapons.
- [ ] More infantry weapon parts.
- [ ] More ammo types.
- [ ] More armor/material options.
- [ ] Fluid/network systems if needed.
- [ ] Thermal systems if needed.

Completion goal:

The sandbox has enough parts and systems to support varied creations and combat scenarios.

---

### R45 — Gameplay Loop

Status: `[ ]`

- [ ] Core loop defined.
- [ ] Creative/sandbox mode works.
- [ ] Survival/progression direction decided.
- [ ] Resource gathering prototype exists if needed.
- [ ] Manufacturing prototype exists if needed.
- [ ] Missions/objectives prototype exists if needed.
- [ ] Combat scenarios exist.
- [ ] AI/factions prototype exists if needed.
- [ ] Progression/economy prototype exists if needed.

Completion goal:

The sandbox gains enough structure, goals, or scenarios to feel like a playable game rather than only a tech demo.

---

### R46 — AI and Targets

Status: `[ ]`

- [ ] Simple target dummy exists.
- [ ] Simple hostile entity exists.
- [ ] AI can move or aim at basic level.
- [ ] AI can use infantry weapons or mounted weapons.
- [ ] AI can damage structures/vehicles.
- [ ] AI behavior is enough to test combat scenarios.
- [ ] AI does not need to be final-quality for V1.

Completion goal:

There are enough non-player threats or targets to test combat, damage, weapons, and vehicle systems.

---

## Phase 11 — Performance, Stability, and Polish

This phase makes V1 stable enough to release or share.

### R47 — Performance

Status: `[ ]`

- [ ] Profiling done.
- [ ] Broadphase optimized.
- [ ] Chunk updates optimized.
- [ ] Sleeping systems optimized.
- [ ] Connectivity optimized.
- [ ] Damage propagation optimized.
- [ ] High-rate simulation optimized.
- [ ] Audio voice count/mixing optimized.
- [ ] Rendering performance optimized.
- [ ] Medium-scale combat scenario performs acceptably.

Completion goal:

The simulation can handle medium-scale structures, vehicles, components, damage events, audio, graphics, and selected high-rate systems.

---

### R48 — Stability and Error Handling

Status: `[ ]`

- [ ] Common crashes fixed.
- [ ] Save/load failure handling exists.
- [ ] Missing asset handling exists.
- [ ] Invalid build/component states handled safely.
- [ ] Physics instability cases reduced.
- [ ] Long-session stability tested.
- [ ] Logs and diagnostics are useful for player bug reports.

Completion goal:

The game can run for normal play sessions without frequent crashes, corrupt saves, or unrecoverable states.

---

### R49 — Testing and Validation Pass

Status: `[ ]`

- [ ] Core engine tests pass.
- [ ] Math tests pass.
- [ ] Physics tests pass.
- [ ] Structure/destruction tests pass.
- [ ] Component tests pass.
- [ ] Save/load tests pass.
- [ ] Player interaction tests pass.
- [ ] Weapon/gunplay tests pass.
- [ ] Audio smoke tests pass.
- [ ] Rendering smoke tests pass.
- [ ] V1 gameplay scenario checklist passes.

Completion goal:

The major V1 systems are validated enough that bugs can be fixed against known expectations instead of guessed manually.

---

### R50 — V1 Content and Scenario Pass

Status: `[ ]`

- [ ] At least one small vehicle can be built and saved.
- [ ] At least one armed vehicle can be built and saved.
- [ ] At least one structure can be built, damaged, and saved.
- [ ] At least one infantry weapon can be customized and used.
- [ ] At least one mounted weapon can be used.
- [ ] At least one guided/control system demo works.
- [ ] At least one combat scenario works.
- [ ] At least one building/tutorial/example scenario exists.
- [ ] Starter parts are balanced enough for testing.

Completion goal:

V1 has enough content and example scenarios to demonstrate the intended sandbox experience.

---

### R51 — V1 Release Candidate

Status: `[ ]`

- [ ] V1 feature scope frozen.
- [ ] Critical bugs fixed.
- [ ] Save compatibility checked.
- [ ] Settings are usable.
- [ ] Controls are usable.
- [ ] Performance target checked.
- [ ] Basic documentation or controls guide exists.
- [ ] Known issues listed.
- [ ] Build packaging works.
- [ ] Release candidate build created.

Completion goal:

The project has a stable V1 candidate that can be played, tested, shared, or released.

---

## Phase 12 — V1 Complete

### R52 — V1

Status: `[ ]`

- [ ] Player can start a world.
- [ ] Player can build a structure.
- [ ] Player can build a vehicle.
- [ ] Player can drive or control the vehicle.
- [ ] Player can damage structures and vehicles.
- [ ] Player can use infantry weapons.
- [ ] Player can customize at least one infantry weapon family.
- [ ] Player can use at least one mounted/vehicle weapon.
- [ ] Components can be damaged or fail.
- [ ] Structures can break apart.
- [ ] Audio communicates important events.
- [ ] Graphics are readable and stylistically coherent.
- [ ] UI supports the main gameplay actions.
- [ ] Save/load works for normal play.
- [ ] Performance is acceptable for the target V1 scale.
- [ ] The game has at least one complete playable scenario or sandbox loop.

Completion goal:

GameWIP reaches a complete single-player V1: a playable vehicle-focused sandbox with destruction, components, player interaction, infantry gunplay, audio, graphics, UI, content, and stable save/load.

---

## Post-V1 Phase — Multiplayer and Expansion

These milestones are not required for V1 unless the project direction changes.

### PV1 — Multiplayer Foundation

Status: `[ ]`

- [ ] Authority model chosen.
- [ ] Networking library/approach selected.
- [ ] Player state synced.
- [ ] Structures synced.
- [ ] Vehicles synced.
- [ ] Components synced.
- [ ] Damage synced.
- [ ] Logic synced.
- [ ] Interpolation/prediction implemented.
- [ ] Multiplayer test works.

Completion goal:

A first multiplayer model works with players, structures, vehicles, components, damage, and logic synchronization.

---

### PV2 — Advanced World and Gameplay Expansion

Status: `[ ]`

- [ ] Larger worlds.
- [ ] Better terrain.
- [ ] More factions/AI.
- [ ] More resources/economy.
- [ ] More advanced weapons.
- [ ] More advanced sensors.
- [ ] More advanced damage systems.
- [ ] More world events.
- [ ] Longer-term progression.

Completion goal:

Post-V1 development expands the game beyond the first complete sandbox foundation.

---

## How to extend this file

When adding new roadmap work:

1. Add it to the closest existing phase.
2. Use a stable roadmap ID.
3. Keep each milestone focused on one clear outcome.
4. Put detailed proof requirements in `testing_checklist.md`.
5. Put implementation progress in `implementation_checklist.md`.
6. Put architectural decisions in `decisions.md`.
7. Avoid adding tiny temporary tasks unless they are needed to define the milestone.
8. Add a short completion goal for every new milestone.
9. Do not move multiplayer into V1 unless V1 scope is intentionally changed.
10. Keep vehicles as the primary identity, but keep player/gunplay strong enough to support infantry combat, boarding, repair, and close-quarters gameplay.

A good roadmap item explains what capability the project gains.

A bad roadmap item only describes one small coding step with no visible milestone outcome.
