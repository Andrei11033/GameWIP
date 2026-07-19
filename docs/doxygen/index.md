# GameWIP Developer Manual

This generated manual is the authoritative developer reference for GameWIP. It
serves contributors, maintainers, and first-party reusable-library consumers who
need to build, validate, extend, package, document, or release the project.

The root README is the short repository entry point. This manual owns detailed
engineering workflows and contracts; it is not player-facing game documentation.

## Choose a path

| Reader or task | Start here | Continue with |
| --- | --- | --- |
| First checkout | @ref project_getting_started | @ref project_environment_setup, then @ref project_build |
| Contributor | @ref project_structure | @ref project_validation, @ref project_testing, then @ref project_contributing |
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

## Common tasks

- Set up a checkout: @ref project_getting_started
- Configure, build, and run: @ref project_build
- Run correctness validation: @ref project_validation
- Add or change tests: @ref project_testing
- Add a library, API, backend, workflow, or documentation page: @ref project_extending
- Update documentation correctly: @ref project_documentation
- Check static analysis and repository rules: @ref project_static_analysis
- Contribute through GitHub: @ref project_contributing
- Review licensing and accepted-history policy: @ref project_decisions
- Maintain repository settings and automation: @ref project_repository_maintenance
- Prepare or finalize a release: @ref project_release_automation

## Generated reference

The generated namespace, class, file, and member reference covers supported
consumer entry headers and selected source-tree integration headers. Generated
export scaffolding belongs to each library's ABI page. Read the owning workflow
or library manual first, then use the generated reference for exact symbols.
