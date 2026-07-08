@page project_documentation Documentation system

GameWIP uses Doxygen for the generated developer manual and ordinary Markdown files for long-form project records. The generated manual is coder-facing documentation for contributors, maintainers, and reusable-library consumers; it is not player-facing game documentation. It documents project workflows, reusable-library manuals, and public API reference material. The ordinary `docs/` files record product direction, milestone criteria, stable decisions, versioning, and contributor workflow. Active task tracking belongs in GitHub issues.

This page defines the documentation standard used across the repository.

## Scope

Use this page when writing or reviewing:

- Generated project manual pages under `docs/doxygen/`.
- Library manuals under `<library>/docs/`.
- Public header comments.
- Approved internal test-hook documentation.
- Source comments for non-obvious implementation contracts.

Use @ref project_planning to decide whether product planning or policy material belongs in ordinary `docs/` files instead of the generated manual.

## Core rules

- Write each rule, workflow, or contract in one authoritative place.
- Summaries may appear on nearby pages, but they must link back to the owner instead of restating the full rule.
- Register Doxygen inputs explicitly; do not rely on recursive source-tree discovery.
- Keep consumer documentation focused on supported public behavior.
- Keep implementation mechanics in internal headers, source comments, backend contracts, or maintainer-only pages.
- Keep examples copy-pasteable when the page is teaching a workflow or public API.
- Keep generated documentation warning-free.

## Documentation ownership

| Area | Owner |
| --- | --- |
| `README.md` | Short repository entry point. |
| `CONTRIBUTING.md` | Short contributor entry point. |
| `docs/doxygen/` | Generated project manual pages. |
| `docs/` | Vision, roadmap, decisions, versioning, and contributor workflow records. |
| `<library>/docs/` | Library manual pages. |
| Public headers | Compact generated API reference and IntelliSense documentation. |
| Internal headers and source files | Maintainer comments for implementation details. |

Project manual pages own repository workflows and contracts. Library manuals own library-specific API usage, examples, troubleshooting, validation coverage, and approved test hooks.

## Page IDs and file names

Generated Doxygen pages use stable lowercase snake_case IDs.

```markdown
@page project_build Build configurations
@page filesystem_public_api FileSystem public API
@page logger_test_hooks Logger test hooks
```

Use these prefixes:

| Page type | Prefix |
| --- | --- |
| Project pages | `project_` |
| IO pages | `io_` |
| FileSystem pages | `filesystem_` |
| Terminal pages | `terminal_` |
| Logger pages | `logger_` |
| Assert pages | `assert_` |
| TestSupport pages | `test_support_` |

A library child page should start with the library page ID. For example, Logger child pages use IDs such as `logger_quick_start`, `logger_public_api`, and `logger_troubleshooting`.

## Required library documentation

Each reusable library should provide:

```text
<library>/docs/
  <library>.md
  quick_start.md
  public_api.md
  examples.md
  testing.md
  troubleshooting.md
```

Add `test_hooks.md` only when the library exposes approved source-tree-only validation hooks.

A library landing page should contain:

- A short library summary.
- Consumer manual links.
- Maintainer validation links.
- Generated API reference links.
- Key behavior.
- Dependency boundary.

A quick-start page should contain:

- Include path.
- Installed CMake usage.
- Source-tree CMake usage.
- Minimal usage.
- Failure handling.
- Where to go next.

## Public API documentation standard

Every public API must be accounted for in generated documentation. This includes public namespaces, classes, structs, enums, enum values, constants, macros, free functions, constructors, member functions, fields, option types, and result types.

Header comments provide compact reference documentation. Manual pages provide practical usage guidance.

Document these properties when relevant:

- Purpose.
- Parameters and return values.
- Status or result behavior.
- Ownership and lifetime.
- Thread-safety and blocking behavior.
- Failure behavior.
- Performance expectations.
- Required initialization or shutdown.
- Relationship to other APIs.
- Example usage.

Related overloads may share one manual entry when they have the same behavior, but every overload must still be named or clearly accounted for.

## Examples policy

Examples are required for non-trivial public behavior.

Add an example when an API:

- Opens, closes, reads, writes, flushes, seeks, creates, deletes, copies, moves, or renames a resource.
- Returns a status or result type with meaningful failure modes.
- Uses an options struct.
- Affects process-wide or global state.
- Affects threading, queues, locks, terminal state, filesystem state, environment variables, or logger lifecycle.
- Has non-obvious ownership or lifetime rules.
- Is a macro with expression-evaluation behavior.
- Is an approved internal test hook used by validation code.

Examples may be omitted for simple getters, obvious value containers, and overloads that only forward to a documented primary operation. The symbol still needs documentation.

## Test-hook documentation

Test hooks are supported source-tree maintainer interfaces. They are not consumer API, are not installed, and are not public compatibility promises.

A test-hook page should document:

- The compile-time option or definition that enables the hooks.
- The internal include path used by validation code.
- Whether hooks are one-shot, persistent, scoped, or query-only.
- The reset rule that prevents state leakage between tests.
- The intended validation scenarios.
- Restrictions on installed-package or consumer use.

Use this structure:

```markdown
@page <page_id>_test_hooks <Library name> test hooks

## Availability
## Include
## Reset rule
## Hook groups
## API reference
## Example
## Related pages
```

## Project workflow pages

Project workflow pages document repeatable actions such as building, testing, validation, benchmarking, profiling, coverage, static analysis, repository automation, and documentation generation.

Use this structure when it fits:

```markdown
@page project_<name> <Title>

## Scope
## Common workflow
## Commands
## Options and flags
## Outputs and artifacts
## Failure behavior
## Maintainer notes
## Related pages
```

Do not force empty sections. Prefer a shorter page over a complete template with filler.

## Project contract pages

Project contract pages document rules that keep the repository consistent, such as structure, packaging, platform backend policy, documentation policy, extension rules, and architectural decisions.

Use this structure when it fits:

```markdown
# or @page <id> <Title>

## Purpose
## Scope
## Core rules
## Required structure
## Allowed exceptions
## Review checklist
## Related pages
```

## Registering a library

A library target registers its public headers and docs folder with `gamewip_register_doxygen_library()`:

```cmake
gamewip_register_doxygen_library(
    NAME Logger
    PAGE_ID logger
    PUBLIC_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/logger.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/logger_macros.h"
    DOCS
        "${CMAKE_CURRENT_SOURCE_DIR}/docs"
)
```

The registered docs folder must contain a landing page named `<PAGE_ID>.md`.

## Source comments

Public headers should document contracts that are useful in generated reference pages and IntelliSense. Internal headers and implementation files should document non-obvious ownership, locking, state transitions, platform behavior, fallback behavior, units, and performance constraints.

Do not comment obvious assignments, getters, or local variables.

## Editorial standard

- Use sentence case for page titles and headings. Preserve exact names such as `IO`, `FileSystem`, `Terminal`, `Logger`, `Assert`, and `TestSupport`.
- Prefer direct, neutral, professional wording.
- Describe implemented behavior in the present tense.
- Use `must` for required contracts, `may` for permitted behavior, and `should` for recommendations.
- Avoid first-person wording.
- Make troubleshooting headings describe the symptom.
- Use `-` for unordered lists.
- End complete-sentence list items with periods.
- Leave short labels and fragments without punctuation.
- Use fenced code blocks with the correct language where possible.
- Keep consumer examples on supported public APIs.

## Doxygen verification

Documentation generation is opt-in through `GAMEWIP_BUILD_DOCS`. Normal builds must not require Doxygen.

The generated Doxyfile should keep explicit inputs, write HTML output under the build tree, and write warnings to `build/docs/docs/doxygen/doxygen_warnings.log`.

The documentation build should be warning-free.

See @ref project_extending for add/change checklists.
