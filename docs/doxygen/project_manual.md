@page project_manual Project manual

This section explains the repository as a working project: how to prepare it,
where code belongs, how builds and executables fit together, how correctness is
proved, and how changes move through GitHub and releases. The pages are ordered
from first checkout toward progressively more specialized maintenance work, but
each page can also be read on its own.

## Onboarding and structure

- @subpage project_getting_started — Build, run, and validate a fresh checkout.
- @subpage project_environment_setup — Understand bootstrap actions, installed
  tools, editor integration, updates, repairs, and uninstall boundaries.
- @subpage project_structure — Learn what each source area owns and which
  dependency directions are allowed.
- @subpage project_build — Understand presets, options, artifacts, runtime
  dependencies, and build failures.
- @subpage project_command_line_tools — Find every supported helper and
  executable command, option, default, and side effect.
- @subpage project_tools — Maintain development-tool versions, providers,
  persistent installations, and disposable helper storage.
- @subpage project_game_executable — Follow argument handling, startup
  validation, runtime initialization, shutdown, and exit behavior.

## Validation and compatibility

- @subpage project_validation — Understand test-runner selection, isolation,
  reporting, failure, and exit semantics.
- @subpage project_testing — Learn how modules, suites, child scenarios,
  package checks, and CTest compose.
- @subpage project_library_compatibility — Understand installed packages,
  targets, public headers, ABI boundaries, and exact-version policy.

## Contribution and repository workflows

- @subpage project_contributing — Follow issue, branch, pull-request, review,
  validation-evidence, and merge rules.
- @subpage project_repository_maintenance — Maintain repository presentation,
  settings, labels, milestones, protection, and periodic checks.
- @subpage project_repository_automation — Understand project reconciliation,
  workflow safety classes, permissions, and dry runs.
- @subpage project_release_automation — Prepare, validate, publish, verify, and
  recover a release.

## Related sections

- @ref project_contracts
- @ref project_quality_workflows
- @ref project_reusable_libraries
- @ref project_planning
