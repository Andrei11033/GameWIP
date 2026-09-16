@page project_documentation Documentation system

GameWIP uses Doxygen for its generated developer manual and Markdown for
long-form project records. The manual serves contributors, maintainers, and
reusable-library consumers. It is not player-facing documentation.

The manual contains project workflows, library manuals, and public API
reference. Files under `docs/` record product direction, milestone criteria,
stable decisions, versioning, and contributor policy. GitHub issues track active
work.

These rules keep the sources working as one documentation system.

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

- Each rule, workflow, or contract must have one authoritative home.
- Nearby summaries may link to that owner, but must not repeat the full rule.
- Doxygen inputs must be registered explicitly; they must not rely on recursive
  source-tree discovery.
- Navigation between registered manual pages must use `@ref` and `@subpage`;
  ordinary Markdown links are reserved for documents outside the generated
  manual.
- Consumer documentation must stay focused on supported public behavior.
- Implementation mechanics must stay in internal headers, source comments,
  backend contracts, or maintainer-only pages.
- Examples must remain usable when a page teaches a workflow or public API.
- Generated documentation must contain no unexpected warnings.

## Information layers

A single page should not serve every level of detail. Put information where a
reader will look for it and link the layers together:

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
must be able to learn the model from the manual, while a reader starting from a
known symbol must be able to find its local contract in the generated reference
or IntelliSense.

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

A reader may enter through any of these forms. A tutorial should not be required
to reach a reference table, and a reference page should not carry a long design
essay. Link the forms together when a task depends on a concept or a concept has
a concrete procedure.

These distinctions are only a way to choose a useful entry point. GameWIP keeps
its existing project and library hierarchy while applying them inside that
structure.

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

Project manual pages own repository workflows and contracts, including
executable and validation source interfaces under `game/`. Library manuals own
library-specific API usage, examples, troubleshooting, validation coverage, and
approved test hooks. Project-owned Markdown follows the editorial and local-link
rules; only pages registered in the generated manual use Doxygen page and
navigation markup.

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

A library child page ID must start with the library page ID. For example, Logger
child pages use IDs such as `logger_quick_start`, `logger_public_api`, and
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

Libraries may add additional manual pages when the public or maintainer-facing
contract needs a focused owner. Extra pages must make the manual easier to use
or maintain; do not create pages just to mirror the source tree.

For example, a library with many build options may add a configuration page, a library with exported runtime symbols may add an ABI or
package-boundary page, and a library with source-tree-only validation hooks may add a test-hooks page.

A library landing page must contain:

- A short library summary.
- A plain-language explanation of the library's role and mental model.
- Consumer manual links that say what question or concept each page explains.
- Maintainer validation links.
- Generated API reference links.
- Key behavior.
- The dependency boundary.

A quick-start page must contain:

- Include path.
- Installed CMake usage.
- Source-tree CMake usage.
- Minimal usage.
- Failure handling.
- Where to go next.

## Public API and ABI documentation standard

Generated documentation must account for every public API and ABI-facing
contract. The same rule applies to source-tree interfaces between the executable,
runners, and validation modules, even though those interfaces are not installed
compatibility promises.

Coverage must include namespaces, classes, structs, enums and non-obvious
enum-value semantics, constants, macros, free functions, constructors, member
functions, fields, option and result types, binary-boundary assumptions, and
exported-symbol expectations.

Header comments provide the detailed reference for API and ABI contracts. Manual
pages provide practical usage guidance, examples, troubleshooting, and broader
workflow context.

GameWIP deliberately keeps public headers useful at the point of use. A
consumer should be able to understand a symbol from IntelliSense without having
to open the manual, while the manual explains how related symbols work together.

Where they matter, document purpose, parameters and return values, status or
result behavior, ownership and lifetime, thread-safety and blocking behavior,
failure behavior, performance expectations, required initialization or shutdown,
relationships to other APIs, and example usage.

The first sentence should work in an IntelliSense popup: state what the symbol
represents or does without sending the reader to another page. Put
qualifications after that sentence. A manual page does not replace a function's
parameter, return, ownership, failure, or thread-safety contract. Conversely,
symbol comments do not need to become long tutorials when a focused manual page
can explain the shared model once.

Use a one-line `@brief` when a simple public query benefits from hover text.
Approved equality and bitmask operators (`|`, `|=`, `&`, and `&=`) may remain
uncommented; their signatures and generated reference entries are still
visible.

Related overloads may share one manual entry when they have the same behavior,
but every overload is still named or clearly accounted for.

## Examples policy

Add an example when it materially clarifies correct usage, sequencing,
ownership, failure handling, state changes, or another non-obvious contract.

Examples are specifically required for public APIs with non-obvious ownership
or lifetime rules, multi-step lifecycles, process-wide or global state,
concurrency, macros with expression-evaluation behavior, or another tricky
contract where a small example is the clearest way to show correct use.
Approved internal test hooks used by validation code need examples when their
setup or reset behavior is not obvious.

Examples may be omitted for simple getters, obvious value containers,
straightforward one-step operations, and overloads that only forward to a
documented primary operation. The symbol still needs documentation.

## Test-hook documentation

Test hooks are supported source-tree maintainer interfaces. They are not consumer
API, are not installed, and are not public compatibility promises.

A test-hook page must document the enabling compile-time option or definition, the
internal include path used by validation code, whether hooks are one-shot,
persistent, scoped, or query-only, the reset rule between tests, the scenarios
they support, and their restrictions in installed-package or consumer code.

The usual structure is:

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

Project workflow pages document repeatable actions such as building, testing,
validation, benchmarking, profiling, coverage, static analysis, repository
automation, and documentation generation.

The common shape is:

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

Empty sections are left out. A shorter page is better than a template filled
with material that does not help the reader.

## Project contract pages

Project contract pages document rules that keep the repository consistent, such
as structure, packaging, platform backend policy, documentation policy,
extension rules, and architectural decisions.

The common shape is:

```markdown
# or @page <id> <Title>

## Purpose
## Scope
## Core rules
## Required structure
## Allowed exceptions
## Review criteria
## Related pages
```

## Registering project source interfaces

Executable-owned headers that define stable source-tree integration contracts may
be registered explicitly with `gamewip_register_doxygen_inputs()`. Keep that
list narrow: register headers used between executable or validation components,
not private implementation headers or every test helper.

Documented `game/` headers must state that they are source-tree interfaces rather than
installed consumer APIs. Approved internal test seams remain outside the
generated API reference and are documented on their owning maintainer page.

## Registering a library

A library target must register its public headers and docs folder with
`gamewip_register_doxygen_library()`:

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

Supported consumer entry headers are the generated-reference owners. Generated
export headers that only provide visibility macros remain transitive build
artifacts. Their installed names and ABI role belong on the owning ABI page;
entry-header and installed-consumer validation exercises them without presenting
them as independent consumer APIs.

## Source comments

File ownership rules:

- Every maintained `.h`, `.h.in`, `.cpp`, and `.inl` file must start with a
  Doxygen `@file` and `@brief` that describe the file purpose.
- Provisional or preserved source outside the supported documented surface must
  gain the same ownership block before that surface is promoted.
- Public headers must document public API and ABI contracts for generated
  reference pages, IntelliSense, maintainers, and readers.
- Internal headers and implementation files must document internal helpers,
  ownership, locking, state transitions, platform behavior, fallback behavior,
  units, and performance constraints.

Statement comments must explain purpose and intent, not narrate nearby syntax.
Use them for reasons a maintainer would otherwise have to rediscover:

- Ordering, lifetime, ownership, or memory ordering.
- Compatibility, platform behavior, encoding, or units.
- Fallback behavior, performance cost, or a tradeoff between valid designs.
- Subtle behavior captured by tests, issue history, or backend documentation.

Obvious assignments, getters, local variables, and direct calls need no comment.
If a statement needs prose just to explain what it does, rewrite it or extract a
named helper first.

When a decision affects observable behavior, keep the source comment focused on
the implementation reason. Document the external contract in the owning manual
or API docs.

Internal helper comments may be shorter than public API comments, but still
explain the helper's purpose, contract, and reason for existing when nearby code
does not make that clear.

Inside non-trivial functions, blank lines can separate phases such as validation,
input preparation, the main operation, fallback or error handling, state
publication, and cleanup.

This spacing is for function-body flow. Section separators belong to larger
file-level groups, not to every unrelated step or statement.

Other maintained first-party languages use their own documentation conventions:

Python files start with a concise module docstring that identifies purpose and
ownership. JavaScript files start with a concise file-purpose or ownership
comment when generated or vendor ownership does not already cover the role.
PowerShell entry points and libraries identify the file purpose and document
non-obvious command contracts, side effects, or failure behavior near the owning
function. Shared CMake modules identify purpose and document helper inputs,
side effects, target ownership, generated files, source-tree versus installed
behavior, platform scope, dependency visibility, and failure conditions when
those are not obvious from the function name. YAML comments explain only
non-obvious policy or security constraints. JSON uses its schema and owning
documentation rather than comment-like fields. Generated and vendored sources
follow their generator or upstream contract.

These are documentation standards, not a reason to add boilerplate CI rules.
Automate drift when doing so protects a meaningful contract.

Long maintained source files may use section separators as a short
responsibility map:

```cpp
// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------
```

Use exactly 60 hyphens, the language's ordinary line-comment marker, and a
concise noun phrase. Public headers group consumer concerns; implementations
group mechanisms; validation sources group suites, fixtures, and runners; CMake
files group configuration, target contracts, dependencies, generated artifacts,
installation, and documentation registration. The same domain terms should be
used across layers when they describe the same responsibility.

Place a separator where it helps a new reader scan the file before reading the
details. A section holds a coherent set of related declarations or steps, not a
single convenience label. Within a section, order policy, state, helpers, and
public entry points in the sequence a maintainer needs to understand the
behavior. Small helper functions with meaningful names are better than long
blocks that need narration.

Small or single-purpose files, individual symbols or namespaces, and generated,
vendored, or comment-free structured content do not need separators. Separators
organize source; they do not replace symbol or helper documentation.

When a public API family also needs a group in the generated reference, pair the
separator with a Doxygen member group:

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
and put broader separators outside them. A single symbol or a repeated file,
namespace, or type name does not need a group.

The repository standards check enforces separator shape and the blank line after
a Doxygen `@{`. `gamewip quality fix` normalizes those mechanically safe details;
malformed three-line separator structures still need manual fixes.

## Editorial standard

Use sentence case for page titles and headings, while preserving exact names such
as `IO`, `FileSystem`, `Terminal`, `Logger`, `Assert`, and `TestSupport`. Keep
the wording direct and professional, describe implemented behavior in the
present tense, and use `must`, `may`, and `should` consistently for required,
permitted, and recommended behavior. Troubleshooting headings should name the
symptom. Use `-` for unordered lists, put periods on complete-sentence items,
and leave short labels and fragments without punctuation. Prefer fenced code
blocks with the right language and keep consumer examples on supported public
APIs.

Documentation describes behavior, contracts, workflows, inputs, outputs, and
expected results. Task-state checkboxes and completion lists belong in
`docs/roadmap.md`; manuals express the same information as prose, rules, or
scenario and expected-result tables. Routine instructions stay direct instead
of centering the author.

## Doxygen verification

Documentation generation is opt-in through `GAMEWIP_BUILD_DOCS`. Normal builds must not require Doxygen.

Build the generated manual and check the warning log:

```powershell
cmake --preset docs
cmake --build --preset docs
$warningLog = Get-Item .\build\docs\docs\doxygen\doxygen_warnings.log
if ($warningLog.Length -ne 0) {
    Get-Content $warningLog
    throw "Doxygen emitted unexpected warnings."
}
```

The generated Doxyfile must keep explicit inputs, write HTML output under the
build tree, and write warnings to
`build/docs/docs/doxygen/doxygen_warnings.log`. Local and CI validation treat a
warning log containing unexpected warnings as a failure. A successful Doxygen
process exit alone is not enough.

The documentation build rejects unexpected Doxygen warnings. Undocumented
passive enum values and explicitly approved equality and bitmask operators are
exceptions to the documentation-warning policy. Non-obvious enum-value
documentation is review-enforced rather than Doxygen-enforced.
Doxygen checks other undocumented declarations and incomplete tagged parameter
or return documentation. A concise summary may fully document an obvious
accessor, but a non-trivial operation still needs its local contract in the
header rather than only in a manual page or parameter names.

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

There is no remote dry-run mode for Pages deployment. The helper therefore
classifies this operation as a deployment and requires the typed phrase
`docs-deploy master`. Configure the existing `github-pages` environment with a
required maintainer reviewer and a `master` deployment-branch restriction.
The workflow then provides the second, GitHub-hosted approval gate. Do not use a
workflow input or repository secret as an entered deployment password.

After the reviewer rule is active, add
`PAGES_PROTECTION_CONFIGURED=required-reviewer` as an environment secret on
`github-pages`. Manual deployment fails closed while this marker is absent;
trusted deployment triggered by a push to `master` remains automatic. Keep the
marker unset when the repository plan does not support required reviewers.

See @ref project_extending for the integration rules that apply to new and changed repository concepts.
