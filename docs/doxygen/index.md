# GameWIP Developer Manual

This manual explains how GameWIP is put together, how its supported libraries
behave, and how to build, validate, extend, and maintain the project. It is
written for contributors and for developers who use the libraries; it is not a
player guide.

The root README gets a new checkout running. Come here when the short answer is
not enough: to understand a design, trace a behavior across components, inspect
an exact API contract, or learn why a project rule exists.

## How the documentation fits together

| Kind of information | Best place to look |
| --- | --- |
| A first build or a familiar command | @ref project_getting_started and @ref project_command_line_tools |
| How a library is designed and used | The library landing page and its focused guides under @ref project_reusable_libraries |
| An exact function, type, field, enum, or macro contract | The generated namespace, class, file, and member reference |
| Repository architecture and engineering decisions | @ref project_structure, @ref project_contracts, and @ref project_planning |
| How to change or verify the project safely | @ref project_extending and @ref project_quality_workflows |

Public header comments are the short reference shown by IntelliSense and by the
generated API pages. The manuals add the surrounding model: how related APIs
compose, which guarantees matter in practice, what the component deliberately
does not own, and how to diagnose failures.

## Find a starting point

| What you need to understand | Start here | Continue with |
| --- | --- | --- |
| First checkout | @ref project_getting_started | @ref project_environment_setup, then @ref project_build |
| Contributor | @ref project_structure | @ref project_validation, @ref project_testing, then @ref project_contributing |
| Command-line user | @ref project_command_line_tools | The owning setup, executable, validation, or benchmark page |
| Reusable-library consumer | @ref project_reusable_libraries | Quick start, public API, examples, troubleshooting, and generated reference |
| Project maintainer | @ref project_contracts | @ref project_extending, @ref project_cmake_infrastructure, and @ref project_documentation |
| Repository maintainer | @ref project_repository_maintenance | @ref project_repository_automation and @ref project_static_analysis |
| Release maintainer | @ref project_versioning | @ref project_release_automation and @ref project_contributing |

## Manual sections

- @subpage project_manual
- @subpage project_reusable_libraries
- @subpage project_contracts
- @subpage project_quality_workflows
- @subpage project_planning

## Common work

- Set up a checkout: @ref project_getting_started
- Configure, build, and run: @ref project_build
- Find a setup, helper, game, test, or benchmark command: @ref project_command_line_tools
- Run correctness validation: @ref project_validation
- Add or change tests: @ref project_testing
- Add a library, API, backend, workflow, or documentation page: @ref project_extending
- Update documentation correctly: @ref project_documentation
- Check static analysis and repository rules: @ref project_static_analysis
- Contribute through GitHub: @ref project_contributing
- Review licensing and accepted-history policy: @ref project_decisions
- Maintain repository settings and automation: @ref project_repository_maintenance
- Prepare or finalize a release: @ref project_release_automation

## Reading the generated reference

The namespace, class, file, and member indexes cover every supported consumer
entry header and a small set of documented source-tree integration headers.
They are the place to confirm exact declarations, parameters, results, enum
values, ownership, error behavior, and threading guarantees. Each library's ABI
page separately explains generated export scaffolding and package boundaries.
