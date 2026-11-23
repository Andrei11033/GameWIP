@page project_planning Project planning

Planning is split across three levels. The vision sets direction, the roadmap
turns that direction into phases and capability slices, and GitHub milestones
and issues track scheduled work.

```text
vision -> phases -> capability slices -> promoted release milestone -> GitHub issues
```

## Product direction

- @subpage project_vision - Product direction, intended experience, technical
  values, and the long-term boundary of the project.
- @subpage project_roadmap - Phases, capability-slice dependency direction,
  current release gates, validation proofs, and explicitly deferred work.

## Active work

Use GitHub issues to track implementation, validation, bugs, cleanup, blockers,
dependencies, and work too detailed for the roadmap. The roadmap defines
capability outcomes and milestone gates; it does not replace issue details.

## Milestones and issue history

The repository keeps no separate task ledger. Create detailed issues for active,
concrete work; leave future ideas in Backlog until they have a release target.
Update the roadmap when capability direction or milestone gates change.

A milestone means concrete work is targeted to that release. Use GitHub's
**Blocked by** relationship for hard dependencies. Keep preferred sequencing
that is not a blocker in the issue or roadmap.

## Related pages

- @ref project_contributing
- @ref project_contracts
- @ref project_decisions
- @ref project_versioning
