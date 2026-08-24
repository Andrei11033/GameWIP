@page project_static_analysis Static analysis and repository checks

GameWIP applies format, static-analysis, documentation, and repository-consistency checks to maintained project files. Third-party code under
`external/` and generated files under `build/` are not project-owned and are excluded.

The checks below cover C++ static analysis, formatting, documentation and
repository rules, local commands, CI behavior, and the narrow cases where a
suppression is acceptable.

Build presets, compiler selection, and output locations are documented in @ref project_build. Correctness tests are documented in @ref
project_validation and @ref project_testing.

## Environment

The GameWIP helper is the preferred Windows entry point because it selects project-owned tool paths, records native command logs, and reports failures
consistently:

```powershell
.\gamewip.bat quality -QualityAction check
.\gamewip.bat quality -QualityAction fix
.\gamewip.bat analyze
```

`quality check` is the complete non-mutating repository quality gate.
`quality fix` applies only deterministic formatters and then runs the same
complete check. The interactive GameWIP `Q` menu exposes both workflows directly.

The `analyze` preset selects `clang++` and requires the UCRT64 packages for CMake, Ninja, Clang, clang-tools-extra, GCC runtime support, Git, and
Python. AddressSanitizer is the exception: it uses the MSYS2 CLANG64 environment and is documented in @ref project_build.

When invoking CMake or clang tools directly instead of through `gamewip.bat`, run them from the MSYS2 UCRT64 environment or put that toolchain first
on `PATH`. Do not mix the analyze configure with an unrelated Visual Studio, standalone LLVM, or different MSYS2 environment. The toolchain that
configures the preset should also provide `clang-tidy`, `run-clang-tidy`, and `clang-format`.

## C++ static analysis

The `analyze` preset configures a compilation database under `build/analyze` and enables the `static-analysis` build target.

Run the full local C++ analysis target from the repository root:

```powershell
.\gamewip.bat analyze
```

For low-level investigation from a correctly configured UCRT64 shell, the equivalent CMake workflow is:

```bash
cmake --preset analyze
cmake --build --preset analyze
```

The target includes:

| Check | Scope |
| --- | --- |
| `clang-tidy` | C and C++ translation units under maintained roots such as `foundation/`, `tools/`, `engine/`, and `game/`. Headers in those roots are checked when included by a translation unit. |
| `clang-format-check` | `.cpp`, `.h`, `.hpp`, and `.inl` files under the same maintained roots. |

The VS Code workspace disables the C/C++ extension's integrated clang-tidy
runner. Workspace IntelliSense uses the GCC-backed `dev` compilation database,
while supported static analysis uses the Clang-backed `analyze` database.
Mixing the GCC database with the extension's Clang frontend can produce false
diagnostics in GCC standard-library and intrinsic headers. Run the
`GameWIP: Analyze` task for editor-integrated static analysis output.

Run one C++ check target when investigating a focused failure:

```bash
cmake --build --preset analyze --target clang-tidy
cmake --build --preset analyze --target clang-format-check
```

`clang-tidy` reads the root `.clang-tidy` file, uses the analyze compilation database, and filters diagnostics to GameWIP-owned source and header
roots. Diagnostics selected by `.clang-tidy` are errors. Suppress a diagnostic only at the narrowest justified location and include a short reason in
the `NOLINT` comment.

Headers are analyzed when they are included by a compiled translation unit. Public headers should also have matching validation translation units
under `game/validation/public_headers/` so the header can be checked as an include boundary instead of only through incidental implementation
includes.

Win32 resource scripts are compiled by the Windows resource compiler and are intentionally not passed to clang-tidy. The root manifest and generated
CMake inputs are validated by the normal configure and build path.

## Formatting fixes

The static-analysis target checks formatting but does not rewrite files. Check or apply the same maintained-source scope through the project helper:

```powershell
.\gamewip.bat format -FormatAction check
.\gamewip.bat format -FormatAction apply
```

Both actions use the repository `.clang-format` and the GameWIP-owned `.cpp`, `.h`, `.hpp`, and `.inl` files under `foundation/`, `tools/`, `engine/`,
and `game/`. `check` is non-mutating; `apply` reports the maintained files whose content changed.

Review formatter output before committing:

```powershell
git diff --check
git diff -- foundation tools engine game
```

Do not run project formatting over `external/`, generated build trees, generated documentation output, or other third-party artifacts. The checked-in
Unicode property header is a deliberate maintained-source exception: its regeneration workflow applies the repository formatter before reproducibility
comparison.

## Repository checks

The `Validation / Repository Checks` GitHub job runs the complete maintained
repository quality policy rather than only language-agnostic spot checks:

- clang-format for maintained C/C++ formatting.
- Ruff lint/format checks for maintained Python.
- PSScriptAnalyzer formatting and warning/error analysis for maintained PowerShell.
- ESLint plus Prettier for maintained JavaScript and structured text.
- Gersemi for maintained CMake files.
- yamllint with the shared 150-column rule enforced as an error.
- markdownlint-cli2 and local relative Markdown link validation.
- actionlint validation for GitHub Actions workflows.
- JSON Schema plus semantic relationship checks for tracked authorities.
- JavaScript policy/unit tests and PowerShell helper regression tests.
- Immutable action pins, explicit workflow permissions, bounded job timeouts,
  trusted `pull_request_target` boundaries, and required non-empty public files.
- Issue-form area choices aligned with automatic area-label routing.
- Documentation ownership, page registration, sidebar structure/order, concise
  library child titles, required library/quick-start sections, complete command
  catalogs, and supported-source documentation validation.

Third-party `external/` content and generated `build/` output remain outside the
maintained quality scope.

Run the complete repository quality gate locally from the repository root:

```powershell
.\gamewip.bat quality -QualityAction check
```

To apply deterministic formatter changes first:

```powershell
.\gamewip.bat quality -QualityAction fix
```

`.\gamewip.bat links` remains available as a focused Markdown-link diagnostic.
The helper forms use project-owned tool resolution and retain run logs. When
developing the checkers themselves, run their direct interfaces:

```powershell
python -m py_compile .github/scripts/*.py
python .github/scripts/check_documentation_standards.py
python .github/scripts/check_repository_standards.py
python .github/scripts/check_markdown_links.py
git diff --check
```

Python bytecode caches are ignored as generated local artifacts.

Run JavaScript automation checks when changing repository or release automation:

```bash
node --check .github/scripts/project-automation.js
node --check .github/scripts/project-automation.test.js
node --test .github/scripts/project-automation.test.js
node --check .github/scripts/release-preparation.js
node --check .github/scripts/release-preparation.test.js
node --test .github/scripts/release-preparation.test.js
```

Run workflow linting when changing GitHub Actions. CI downloads the pinned
official actionlint release and verifies its SHA-256 before execution. Local
usage requires a compatible actionlint installation:

```bash
actionlint
```

## Documentation checks

The regular validation workflow builds Doxygen and rejects Doxygen warnings. Markdown registered with Doxygen is therefore parsed and cross-reference
checked as part of documentation validation.

The documentation-standards checker validates exactly one unique page ID per
manual file, exactly one sidebar parent per page, intentional sidebar order,
explicit project-page registration, concise library child titles, the required
library file and section sets, complete setup/project-helper command catalogs,
and leading `@file`/`@brief` ownership across every supported documented source
root.

The repository-standards checker keeps GitHub Actions dependencies immutable,
requires explicit permissions and job timeouts, fails closed on empty CTest
selection, preserves trusted `pull_request_target` checkout boundaries, and
verifies that public repository files exist and are non-empty.

The repository link checker validates local relative links in maintained root,
project, GitHub, game, setup/helper, engine, and library Markdown. It ignores external
URLs and `#anchor` fragments; Doxygen-owned anchors and API cross-references are
validated by the documentation build.

When documentation, public headers, Doxygen registration, or manual pages change, also run the documentation preset:

```bash
cmake --preset docs
cmake --build --preset docs
```

Then perform the warning-log check in @ref project_documentation. A successful Doxygen process exit alone does not prove that the generated manual is
warning-free.

Doxygen validates syntax and links, but it does not judge prose consistency. First-party Markdown must also be reviewed against the heading, voice,
terminology, list, example, and ownership rules in @ref project_documentation.

## Local pre-commit checklist

Run the repository-check commands above when changing documentation or
GitHub/setup automation. Add the C++ analysis commands when changing maintained
C++ code, and add the documentation preset when changing public comments,
manual pages, or Doxygen registration. Before a release, run the complete
`local-release-check` bundle documented in @ref project_repository_maintenance.

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
| clang-tidy cannot find `compile_commands.json`. | The analyze preset was not configured, or the command is running against the wrong build tree. | Run `cmake --preset analyze` from the MSYS2 UCRT64 environment. |
| clang-tidy fails on a new diagnostic. | The code violates an enabled check. | Fix the code or add a narrow justified suppression. |
| clang-format check fails. | Maintained C++ formatting differs from `.clang-format`. | Run the formatter locally, review the diff, and commit the result. |
| A public header is not checked directly. | No validation translation unit includes it as a public boundary. | Add or update the matching file under `game/validation/public_headers/`. |
| Python script validation fails. | A repository maintenance script contains invalid syntax for the configured Python. | Fix the script before running repository checks again. |
| actionlint fails. | A workflow syntax or shell issue exists. | Fix the workflow before merging. |
| Repository standards fail. | An Action pin, job policy, or public file drifted. | Restore the reported repository contract. |
| Documentation standards fail. | Ownership, navigation, or coverage drifted. | Fix the owner or update the checker intentionally. |
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

VS Code exposes this workflow as `GameWIP: Analyze` on `Alt+F8`.

## Related pages

- @ref project_build
- @ref project_testing
- @ref project_documentation
- @ref project_repository_automation
