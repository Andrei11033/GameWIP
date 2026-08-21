@page project_vision Vision

GameWIP is a sandbox about designing things that work. Players build vehicles,
weapons, missiles, structures, and control systems, then see how those creations
behave under motion, load, and damage.

The project aims for an unusual balance: construction should be approachable on
the first attempt, while the systems underneath it should reward players who
want to understand and tune them. A useful creation should not require an
engineering course. A carefully engineered creation should still perform
better for understandable reasons.

## What the game should feel like

Building starts quickly. Components are useful with sensible defaults, common
operations take few steps, and the game communicates why something does or does
not work. More detailed controls appear when they are useful rather than being
required up front.

Creations are physical systems, not static collections of blocks. Structure,
mass, connectivity, power, control, and damage should influence one another.
When a part is hit, the result may be deformation, lost strength, detachment, an
exposed internal system, or a functional failure—not merely a missing visual
piece.

Realism serves this feedback loop. It is valuable when it creates a decision a
player can understand, test, and improve. When it only creates repetitive setup
or hidden failure, usability takes priority.

## The experience GameWIP is building toward

- Build vehicles and structures from a shared structural foundation.
- Add mechanical, electrical, sensing, control, and weapon systems.
- Test a creation, inspect what happened, and revise the design.
- Use simple defaults for ordinary builds and deeper configuration for advanced
  systems such as stabilizers, servos, guidance, sensors, and missiles.
- Damage creations in ways that change both their shape and their behavior.
- Learn through clear feedback, debug views, and observable system state.

GameWIP should reward engineering thought without becoming professional CAD
software. The point is an expressive sandbox, not paperwork disguised as
simulation.

## Principles that guide development

### Make the simple case work first

Every major system needs a small, useful version before it gains advanced
configuration. Depth should grow from a working foundation rather than delay
it.

### Keep simulation independent from presentation

Rendering makes the simulation visible; it does not define the simulation.
Timing, physics, damage, and control behavior must remain testable and stable
without depending on render frame rate.

### Share foundations where the concepts are shared

Vehicles, buildings, and other destructible creations should use the same
structural model where practical. A common foundation makes interactions more
consistent and avoids parallel systems that disagree about the world.

### Spend update time where it changes the result

Guidance, stabilization, sensors, and other control loops may need higher-rate
updates. Ordinary gameplay systems should not inherit that cost automatically.

### Make systems observable

Logs, assertions, tests, benchmarks, overlays, and early debug rendering are
part of the engine foundation. A complex simulation can only be improved when
developers and players can see why it behaved as it did.

## Where the details live

This page describes the intended experience and the principles behind it. It
does not track tasks or freeze implementation details.

- @ref project_roadmap defines what each milestone must deliver.
- @ref project_decisions records durable project-wide technical and workflow
  choices.
- @ref project_planning collects planning and contribution references.
- GitHub issues track active implementation, bugs, and follow-up work.

When the direction changes, update this page in terms of the experience being
built. Put technical consequences in the decisions page and concrete work in
the roadmap or an issue.
