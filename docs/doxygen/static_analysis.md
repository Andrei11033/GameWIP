@page project_static_analysis Static analysis and repository checks

GameWIP applies format, static-analysis, documentation, and repository-consistency checks to maintained project files. Third-party code under `external/` and generated files under `build/` are not project-owned and are excluded.

## Scope

This page documents C++ static-analysis checks, non-C++ repository checks, local commands, CI behavior, and suppression policy.

## C++ checks

The `static-analysis` preset configures a compilation database and enables the `static-analysis` build target.

Run the full local static-analysis target with:

```powershell
cmake --preset static-analysis
cmake --build --preset static-analysis
```

The target includes:

| Check | Scope |
| --- | --- |
| `clang-tidy` | C and C++ translation units under maintained roots such as `foundation/`, `tools/`, `engine/`, and `game/`. Headers in those roots are checked when included by a translation unit. |
| `clang-format-check` | `.cpp`, `.h`, `.hpp`, and `.inl` files under the same maintained roots. |

The MSYS2 UCRT64 environment must provide `clang`, `clang-tools-extra`, `cmake`, `ninja`, and `python`.

Diagnostics selected in the root `.clang-tidy` file are errors. Suppress a diagnostic only at the narrowest justified location and include a short reason in the `NOLINT` comment.

Win32 resource scripts are compiled by the Windows resource compiler and are intentionally not passed to clang-tidy. The root manifest and generated CMake inputs are validated by the normal configure and build path.

## Repository checks

The `Validation / Repository Checks` GitHub job runs checks that do not belong to clang:

- Node.js syntax checks and unit tests for repository automation JavaScript.
- JSON parsing for maintained JSON files.
- actionlint validation for GitHub Actions workflows.
- Local relative Markdown link validation for maintained documentation.

Local commands:

```powershell
node --check .github/scripts/project-automation.js
node --check .github/scripts/project-automation.test.js
node --test .github/scripts/project-automation.test.js
python .github/scripts/check-markdown-links.py
actionlint
```

Run the release-preparation script checks when changing release automation:

```powershell
node --check .github/scripts/release-preparation.js
node --check .github/scripts/release-preparation.test.js
node --test .github/scripts/release-preparation.test.js
```

## Documentation checks

The regular validation workflow also builds Doxygen and rejects Doxygen warnings. Markdown registered with Doxygen is therefore parsed and cross-reference checked as part of documentation validation.

The repository link checker validates local relative links in maintained Markdown entry points, generated-manual source pages, and GitHub documentation files. It ignores external URLs and `#anchor` fragments; Doxygen-owned anchors and API cross-references are validated by the documentation build.

Doxygen validates syntax and links, but it does not judge prose consistency. First-party Markdown must also be reviewed against the heading, voice, terminology, list, example, and ownership rules in @ref project_documentation.

## Exclusions

Excluded areas include:

- `external/` third-party source trees.
- Generated build directories.
- Generated documentation output.
- Generated coverage output.
- Other generated artifacts documented by their owning workflow.

Do not fix third-party formatting or analysis warnings by rewriting vendor code. Adjust exclusions or upstream dependency versions instead.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| clang-tidy fails on a new diagnostic. | The code violates an enabled check. | Fix the code or add a narrow justified suppression. |
| clang-format check fails. | Maintained C++ formatting differs from `.clang-format`. | Run the formatter locally and commit the result. |
| actionlint fails. | A workflow syntax or shell issue exists. | Fix the workflow before merging. |
| Markdown link check fails. | A maintained Markdown file points to a missing local target. | Fix the link, add the missing page, or move the target behind an excluded generated/third-party boundary. |
| Doxygen warnings appear. | A page, reference, or public comment is malformed. | Fix the owning documentation or registration. |
| Vendor files are checked. | An exclusion pattern is incomplete. | Update the owning analysis helper without rewriting vendor code. |

## Maintainer notes

When adding a new maintained source root or file type:

- Decide which checks own it.
- Add local and CI validation when practical.
- Exclude generated and third-party output deliberately.
- Document required developer tools and local commands.
- Keep suppressions narrow and justified.

VS Code exposes `Run Static Analysis` as a workspace task.

## Related pages

- @ref project_build
- @ref project_testing
- @ref project_documentation
- @ref project_repository_automation
