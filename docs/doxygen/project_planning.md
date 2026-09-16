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

Use GitHub issues for:

- Implementation tasks.
- Validation tasks.
- Bugs.
- Follow-up cleanup.
- Milestone work that is too detailed for the roadmap page.
- Blockers and dependencies.

The roadmap defines long-term capability outcomes and the conditions for
completing each concrete milestone. It does not duplicate individual issues.

## Milestones and issue history

GitHub issues are the record of active work, so the repository does not keep a
separate source-tree task ledger. Do not pre-create speculative issues for
future capability slices. Only active, concrete milestone work receives
detailed implementation issues. Future ideas may remain in Backlog until
scheduling gives them a clear release target. The roadmap carries the capability
direction and milestone gates, so update it when either changes.

A GitHub milestone means concrete work is targeted to that release. A capability
slice does not automatically receive a milestone, and a useful future issue may
remain in Backlog until scheduling gives it a release target. Use GitHub's
**Blocked by** and blocking relationships for hard dependencies. Keep preferred
sequencing that is not a hard blocker in the issue or roadmap.

## Related pages

- @ref project_contributing
- @ref project_contracts
- @ref project_decisions
- @ref project_versioning
