@page project_vision Vision

GameWIP is a realistic sandbox building game focused on player-made vehicles, weapons, missiles, buildings, and technical systems.

The goal is to combine fast, approachable construction with optional engineering depth. A new player should be able to build something quickly. An advanced player should be able to tune important systems in more detail.

## Core identity

GameWIP is built around five ideas:

- **Simple base building.** Building should feel approachable and fast, similar in spirit to Stormworks and Starbase.
- **Optional engineering depth.** Advanced configuration should exist for important systems, but it should not be required for basic building.
- **Destructible structures and worlds.** Vehicles, buildings, and world structures should be damageable in meaningful ways.
- **Granular damage and component behavior.** Destruction should affect both shape and function. Parts should be able to degrade, detach, fail, expose internal systems, or change connected behavior.
- **High-rate control where it matters.** Stabilizers, missiles, servos, guidance, sensors, and weapons may need higher-frequency logic than ordinary gameplay systems.

## Design goals

- Building must be easy to start with.
- Complexity must be optional, not forced.
- Realism matters when it creates understandable engineering choices.
- Usability matters more when realism only adds friction.
- Damage must affect both structure and function.
- Components should be fast to place, useful by default, and rich to configure when needed.
- Vehicles and buildings should share the same structural foundation where practical.
- The simulation should support believable physics, destruction, and control systems.

## Game focus

GameWIP focuses on:

- Player-made creations.
- Military and technical vehicles.
- Programmable and controllable systems.
- Weapons, missiles, turrets, sensors, and guidance systems.
- Realistic engineering decisions without making the game frustrating to build in.

The game should reward engineering thinking while remaining a sandbox game, not a professional CAD tool.

## Development principles

1. **Simulation first, visuals second.** Early rendering should support development, debugging, and validation before presentation polish.
2. **Simple first, deep later.** Every major system should have a simple usable version before advanced configuration is added.
3. **Shared structural backbone.** Vehicles, buildings, and destructible structures should use the same core structural model where possible.
4. **Damage should matter.** Destruction should affect strength, mass, connectivity, components, and behavior, not only remove visible blocks.
5. **High-frequency simulation is selective.** Reserve high-frequency logic for systems that need it, such as control loops, guidance, stabilization, sensors, and weapons.
6. **Debuggability is part of the foundation.** Early rendering, overlays, logs, assertions, tests, and benchmarks exist to make systems observable.

## How to extend this page

When updating the vision:

- Keep it high-level.
- Avoid implementation details unless they affect the identity of the game.
- Move technical decisions to @ref project_decisions.
- Move milestone criteria to @ref project_roadmap.
- Move active tasks to GitHub issues.

## Related pages

- @ref project_roadmap
- @ref project_decisions
- @ref project_planning
