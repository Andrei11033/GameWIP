# GameWIP Developer Manual

This manual covers how GameWIP is put together, how its libraries behave, and
how to build, validate, extend, and maintain it. It is for contributors and
library users, not players. The root README gets a checkout running; come here
for designs, cross-component behavior, exact API contracts, and project rules.

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

## Reading the generated reference

The namespace, class, file, and member indexes cover every supported consumer
entry header and a few documented source-tree integration headers. Use them for
exact declarations, results, ownership, errors, and threading. Each library's
ABI page covers export scaffolding and package boundaries.
