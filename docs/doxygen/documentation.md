@page project_documentation Documentation system

GameWIP uses Doxygen for its generated developer manual and Markdown for long-form project records. The manual serves contributors, maintainers, and
reusable-library consumers; it is not player-facing documentation.

The manual contains project workflows, library manuals, and public API reference material. Files under `docs/` record product direction, milestone
criteria, stable decisions, versioning, and contributor policy. GitHub issues track active work.

The rules below keep those sources working as one documentation system instead
of a collection of unrelated files.

## Scope

Use this page when writing or reviewing:

- Generated project manual pages under `docs/doxygen/`.
- Library manuals under `<library>/docs/`.
- Project-owned Markdown and local orientation READMEs under first-party source,
  script, and GitHub workflow directories.
- Public and ABI-facing header comments.
- Explicitly documented executable and validation source interfaces.
- Approved internal test-hook documentation.
- Source comments for file purpose, internal helpers, and non-obvious implementation contracts.

Use @ref project_planning to decide whether product planning or policy material belongs in ordinary `docs/` files instead of the generated manual.

## Core rules

- Write each rule, workflow, or contract in one authoritative place.
- Summaries may appear on nearby pages, but they must link back to the owner instead of restating the full rule.
- Register Doxygen inputs explicitly; do not rely on recursive source-tree discovery.
- Use `@ref` and `@subpage` for navigation between registered manual pages; reserve relative Markdown links for ordinary Markdown documents outside
  the generated manual.
- Keep consumer documentation focused on supported public behavior.
- Keep implementation mechanics in internal headers, source comments, backend contracts, or maintainer-only pages.
- Keep examples copy-pasteable when the page is teaching a workflow or public API.
- Keep generated documentation warning-free.

## Information layers

Do not make one page serve every reading depth. Place information where a reader
will naturally look for it, and link between layers:

| Layer | What it must answer |
| --- | --- |
| Repository entry points | What the project is, what is supported, and where a new reader should go next. |
| Project manual | How the repository works, how components relate, how workflows behave, and why project-wide constraints exist. |
| Library landing page | What the library owns, its mental model, its most important guarantees, and which focused guide answers each deeper question. |
| Focused library guide | A complete explanation of one coherent behavior, including composition, edge cases, examples, and failure handling. |
| Generated API reference and IntelliSense | The exact contract of every supported declaration at the point of use. |
| Maintainer and test-hook pages | Internal validation seams, backend constraints, and implementation-facing procedures that consumers should not depend on. |

Task-oriented links help readers enter the documentation, but they do not
replace conceptual explanation. A reader who does not yet know the right API
must be able to learn the model from the manual; a reader already holding a
symbol must be able to learn its complete local contract from the generated
reference or IntelliSense.

## Write for the reader's question

Before writing a page or section, decide which need it serves. GameWIP follows
the widely used distinction between tutorials, how-to guidance, reference, and
explanation:

| Reader's question | Documentation form | GameWIP examples |
| --- | --- | --- |
| “Can you teach me the first working path?” | Tutorial | Getting started and each library quick start. |
| “How do I accomplish this particular task?” | How-to guide | Examples, setup procedures, validation commands, and troubleshooting. |
| “What exactly does this accept or guarantee?” | Reference | Public API pages, generated declarations, command tables, and configuration tables. |
| “Why does the system work this way?” | Explanation | Library concept pages, architecture, decisions, compatibility, and backend contracts. |

A reader may enter through any of these forms. Do not force someone through a
tutorial to reach a reference table, and do not make a reference page carry a
long design essay. Link the forms together when a task depends on a concept or
a concept has a concrete procedure.

This organization is informed by the
[Diátaxis documentation framework](https://diataxis.fr/) and the way mature
language documentation, such as the
[Python documentation](https://docs.python.org/3/), separates tutorials,
how-to material, and technical reference. GameWIP keeps its existing project
and library hierarchy while applying those reader-focused distinctions inside
it.

## Documentation ownership

| Area | Owner |
| --- | --- |
| `README.md` | Repository entry point. |
| `CONTRIBUTING.md` | Short contributor entry point. |
| `docs/doxygen/` | Generated project manual pages. |
| `docs/` | Vision, roadmap, decisions, versioning, and contributor workflow records. |
| `<library>/docs/` | Library manual pages. |
| Other first-party Markdown | Local orientation, templates, or component entry points that follow the editorial/link rules and delegate detailed contracts to their authoritative manual owner. |
| Public headers | Detailed generated API and ABI reference plus IntelliSense documentation. |
| Documented `game/` headers | Generated reference for executable-owned and validation source interfaces; these are not installed consumer APIs. |
| Internal headers and source files | File purpose, internal helper contracts, and maintainer comments for implementation details. |

Project manual pages own repository workflows and contracts. They also own executable and validation source interfaces under `game/`. Library manuals
own library-specific API usage, examples, troubleshooting, validation coverage, and approved test hooks. Every project-owned Markdown file is subject
to the editorial and local-link rules; only pages deliberately registered in the generated manual use Doxygen page/navigation markup.

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
| Unicode pages | `unicode_` |
| IO pages | `io_` |
| FileSystem pages | `filesystem_` |
| Terminal pages | `terminal_` |
| Desktop pages | `desktop_` |
| Logger pages | `logger_` |
| Assert pages | `assert_` |
| TestSupport pages | `test_support_` |

A library child page ID must start with the library page ID. For example,
Logger child pages use IDs such as `logger_quick_start`, `logger_public_api`, and
`logger_troubleshooting`. The displayed child title must omit the repeated
library name because the Doxygen sidebar already supplies that context:

```markdown
@page logger_quick_start Quick start
@page logger_public_api Public API
@page logger_troubleshooting Troubleshooting
```

Keep the library name in the landing-page title and in prose where the page may
be read outside its navigation context.

Desktop uses `desktop_library` for its landing page so it cannot be confused
with generated namespace reference names. Its child pages use the `desktop_`
prefix consistently.

## Required library documentation

Each reusable library must provide:

```text
<library>/docs/
  <page-id>.md
  quick_start.md
  public_api.md
  examples.md
  testing.md
  troubleshooting.md
```

The landing filename must match the `PAGE_ID` registered for the library. For
most libraries this is the lowercase library name; Desktop uses the documented
`desktop_library` form above.

Libraries may add additional manual pages when the public or maintainer-facing contract needs a focused owner. Extra pages must make the manual easier
to use or maintain; do not create pages just to mirror the source tree.

For example, a library with many build options may add a configuration page, a library with exported runtime symbols may add an ABI or
package-boundary page, and a library with source-tree-only validation hooks may add a test-hooks page.

A library landing page must contain:

- A short library summary.
- A plain-language explanation of the library's role and mental model.
- Consumer manual links that say what question or concept each page explains.
- Maintainer validation links.
- Generated API reference links.
- Key behavior.
- Dependency boundary.

A quick-start page must contain:

- Include path.
- Installed CMake usage.
- Source-tree CMake usage.
- Minimal usage.
- Failure handling.
- Where to go next.

## Public API and ABI documentation standard

Generated documentation must account for every public API and ABI-facing contract. The same rule applies to source-tree interfaces between the
executable, validation runners, and validation modules, even though those interfaces are not installed compatibility promises.

Coverage includes namespaces, classes, structs, enums and their values, constants, macros, free functions, constructors, member functions, fields,
option and result types, binary-boundary assumptions, and exported-symbol expectations.

Header comments provide the detailed reference documentation for API and ABI contracts. Manual pages provide practical usage guidance, examples,
troubleshooting, and broader workflow context.

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

Write the first sentence so it is useful in an IntelliSense popup: state what
the symbol represents or does without requiring the reader to open another
page. Put qualifications after that sentence. Do not rely on a manual page to
supply a function's parameters, return behavior, ownership, failure behavior,
or thread-safety contract. Conversely, do not turn every symbol comment into a
long tutorial when a focused manual page can explain the shared model once.

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

Examples may be omitted for simple getters, obvious value containers, and overloads that only forward to a documented primary operation. The symbol
still needs documentation.

## Test-hook documentation

Test hooks are supported source-tree maintainer interfaces. They are not consumer API, are not installed, and are not public compatibility promises.

A test-hook page must document:

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

Project workflow pages document repeatable actions such as building, testing, validation, benchmarking, profiling, coverage, static analysis,
repository automation, and documentation generation.

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

Project contract pages document rules that keep the repository consistent, such as structure, packaging, platform backend policy, documentation
policy, extension rules, and architectural decisions.

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

## Registering project source interfaces

Executable-owned headers that define stable source-tree integration contracts may be registered explicitly with `gamewip_register_doxygen_inputs()`.
Keep that list narrow: register headers used between executable or validation components, not private implementation headers or every test helper.

Documented `game/` headers must state that they are source-tree interfaces rather than installed consumer APIs. Approved internal test seams remain
excluded from the generated API reference and are documented on their owning maintainer page.

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

Register supported consumer entry headers as generated-reference owners. Generated export headers that only provide visibility macros remain
transitive build artifacts: list their installed names and ABI role on the owning ABI page, exercise them through the entry headers and
installed-consumer validation, and do not present them as independent consumer APIs.

## Source comments

Every maintained `.h`, `.h.in`, `.cpp`, and `.inl` file must start with a
Doxygen `@file` and `@brief` that describe the file purpose. Provisional or
preserved source outside the supported documented surface must gain the same
ownership block before that surface is promoted.

Public headers must document public API and ABI contracts in enough detail for
generated reference pages, IntelliSense, maintainers, and readers. Internal
headers and implementation files must document internal helpers, ownership,
locking, state transitions, platform behavior, fallback behavior, units, and
performance constraints. Internal helper comments may be shorter than public API
comments, but they must still explain what the helper does and why it exists
when that is not obvious from the surrounding code.

Other maintained first-party languages use their own native documentation
conventions rather than mechanically copying C++ comments:

- Python files start with a concise module docstring that identifies purpose and
  ownership.
- JavaScript files start with a concise file-purpose/ownership comment when the
  role is not already represented by generated/vendor ownership.
- PowerShell entry points and libraries identify the file purpose and document
  non-obvious command contracts, side effects, or failure behavior near the
  owning function.
- Shared CMake modules identify purpose and document helper inputs, side effects,
  and failure conditions when those are not obvious from the function name.
- YAML comments explain only non-obvious policy or security constraints.
- JSON uses its schema and owning documentation; do not invent comment-like
  fields merely to carry prose.
- Generated and vendored sources follow the generator/upstream contract rather
  than first-party hand-formatting or comment rules.

These expectations are review/documentation standards. Do not add a generic CI
rule merely to require boilerplate header comments; automate drift when it
protects a meaningful contract.

Do not comment obvious assignments, getters, or local variables.

Use section separators to give long maintained source files a short
responsibility map:

```cpp
// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------
```

Use exactly 60 hyphens, the language's ordinary line-comment marker, and a
concise noun phrase. Public headers group consumer concerns; implementations
group mechanisms; validation sources group suites, fixtures, and runners. Use
the same domain terms across layers when they describe the same responsibility.

Do not add separators to small or single-purpose files, around individual
symbols or namespaces, or to generated, vendored, or comment-free structured
content. Separators organize source; they never replace symbol or helper
documentation.

When a public API family should also be grouped in generated reference pages,
pair the separator with a Doxygen member group:

```cpp
// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------

/// @name Lifecycle
/// @{

/// @brief Initializes the service.
Status initialize() noexcept;

/// @brief Stops the service.
Status shutdown() noexcept;

/// @}
```

Keep complete symbol comments inside the group. Retain useful existing groups
and put broader separators outside them. Do not group a single symbol or repeat
only the file, namespace, or type name.

The repository standards check enforces separator shape and the blank line
after a Doxygen `@{`. `gamewip quality fix` normalizes both mechanically safe
details; malformed three-line separator structures remain manual fixes.

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

Build the generated manual and reject any recorded warning:

```powershell
cmake --preset docs
cmake --build --preset docs
$warningLog = Get-Item .\build\docs\docs\doxygen\doxygen_warnings.log
if ($warningLog.Length -ne 0) {
    Get-Content $warningLog
    throw "Doxygen emitted warnings."
}
```

The generated Doxyfile must keep explicit inputs, write HTML output under the build tree, and write warnings to
`build/docs/docs/doxygen/doxygen_warnings.log`. Local and CI validation must reject a non-empty warning log; a successful Doxygen process exit alone
is insufficient.

The documentation build must be warning-free. Doxygen checks undocumented
declarations, individual enum values, and incomplete tagged parameter or return
documentation. A concise summary may fully document an obvious accessor, but a
non-trivial operation must not rely on a manual page or on parameter names to
explain its local contract.

## GitHub Pages deployment

`Validation / Docs Check` builds the manual once for each pull request and
`master` push. A successful `master` push retains that validated HTML for one
day; the `Doxygen Docs` workflow consumes the artifact and deploys it without a
second Doxygen build. Failed validation never publishes documentation.

Manual `Doxygen Docs` dispatch is the recovery and deliberate-republication
path. Because it has no preceding validation artifact, it builds and verifies
the manual before deployment. Maintainers can preview and dispatch the
repository-owned command with:

```powershell
.\gamewip.bat workflow run docs-deploy -Preview
.\gamewip.bat workflow run docs-deploy
```

There is no remote dry-run mode for Pages deployment, so the helper classifies
this operation as a deployment and requires the typed phrase
`docs-deploy master`. Configure the existing `github-pages` environment with
a required maintainer reviewer and a `master` deployment-branch restriction.
The workflow then provides the second, GitHub-hosted approval gate. Do not use a
workflow input or repository secret as an entered deployment password.

After the reviewer rule is active, add
`PAGES_PROTECTION_CONFIGURED=required-reviewer` as an environment secret on
`github-pages`. Manual deployment fails closed while this marker is absent;
trusted deployment triggered by a push to `master` remains automatic. Keep the
marker unset when the repository plan does not support required reviewers.

See @ref project_extending for add/change checklists.
