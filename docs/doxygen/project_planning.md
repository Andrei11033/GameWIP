@page project_planning Project planning

Project planning is split between stable documentation and GitHub issue tracking. The generated manual registers the stable planning pages that explain direction and milestone intent. GitHub issues track the active implementation tasks, validation tasks, bugs, and follow-up work inside each milestone.

## Product direction

- @subpage project_vision
- @subpage project_roadmap

## Active task tracking

Use GitHub issues for active task tracking.

Issues should own:

- Implementation tasks.
- Validation tasks.
- Bugs.
- Follow-up cleanup.
- Milestone work items that are too detailed for the roadmap page.
- Blockers and dependencies.

The roadmap should remain the milestone checklist. It explains what must be true before each milestone is complete. It should not duplicate every issue or replace GitHub project tracking.

## Historical checklists

Implementation and testing checklists are no longer separate source-tree planning authorities. GitHub issues track active work, while @ref project_roadmap defines milestone completion criteria.

Do not recreate broad checklist ledgers unless the project deliberately changes planning policy. Focus new planning work in GitHub issues and update the roadmap only when milestone gates change.

## Related pages

- @ref project_contributing
- @ref project_contracts
- @ref project_decisions
- @ref project_versioning
