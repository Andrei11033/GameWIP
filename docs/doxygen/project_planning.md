@page project_planning Project planning

Stable documentation carries planning from vision through broad phases and
unnumbered capability slices. When a slice is understood enough, it is promoted
into a concrete numbered release milestone. GitHub issues then track active
implementation, validation, bugs, and follow-up work within that milestone.

```text
vision -> phases -> capability slices -> promoted release milestone -> GitHub issues
```

## Product direction

- @subpage project_vision — Product direction, intended experience, technical
  values, and the long-term boundary of the project.
- @subpage project_roadmap — Phases, capability-slice dependency direction,
  current release gates, validation proofs, and explicitly deferred work.

## Active task tracking

Use GitHub issues for:

- Implementation tasks.
- Validation tasks.
- Bugs.
- Follow-up cleanup.
- Milestone work items that are too detailed for the roadmap page.
- Blockers and dependencies.

The roadmap defines long-term capability outcomes and what must be true before
each concrete milestone is complete without duplicating individual issues.

## Historical checklists

Do not maintain separate source-tree implementation or testing checklists. GitHub issues track active work, while @ref project_roadmap defines
milestone completion criteria.

Do not pre-create hundreds of speculative issues for future capability slices.
Only active, concrete milestone work receives detailed implementation issues.
Update the roadmap when capability direction or promoted milestone gates
change.

## Related pages

- @ref project_contributing
- @ref project_contracts
- @ref project_decisions
- @ref project_versioning
