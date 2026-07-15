@page project_static_analysis Static analysis and repository checks

GameWIP applies format, static-analysis, documentation, and repository-consistency checks to maintained project files. Third-party code under `external/` and generated files under `build/` are not project-owned and are excluded.

## Scope

This page owns C++ static-analysis checks, local formatting commands, non-C++ repository checks, CI behavior, and suppression policy.

Build presets, compiler selection, and output locations are documented in @ref project_build. Correctness tests are documented in @ref project_validation and @ref project_testing.

## Environment

Run C++ analysis and formatting from the MSYS2 UCRT64 environment used for normal development. The `static-analysis` preset selects `clang++` and requires the UCRT64 packages for CMake, Ninja, Clang, clang-tools-extra, GCC runtime support, Git, and Python.

When running local commands from PowerShell instead of an MSYS2 UCRT64 shell, put UCRT64 first on `PATH` before invoking Python, CMake, or clang tools:

```powershell
$env:PATH = "C:\MSYS2\ucrt64\bin;$env:PATH"
```

AddressSanitizer is the exception: it uses the MSYS2 CLANG64 environment and is documented in @ref project_build.

Do not run project clang-tidy checks from an unrelated Visual Studio, standalone LLVM, or different MSYS2 environment. The tool that configures the preset should be the same toolchain family that provides `clang-tidy`, `run-clang-tidy`, and `clang-format`.

## C++ static analysis

The `static-analysis` preset configures a compilation database under `build/static-analysis` and enables the `static-analysis` build target.

Run the full local C++ analysis target from the repository root:

```bash
cmake --preset static-analysis
cmake --build --preset static-analysis
```

The target includes:

| Check | Scope |
| --- | --- |
| `clang-tidy` | C and C++ translation units under maintained roots such as `foundation/`, `tools/`, `engine/`, and `game/`. Headers in those roots are checked when included by a translation unit. |
| `clang-format-check` | `.cpp`, `.h`, `.hpp`, and `.inl` files under the same maintained roots. |

Run one C++ check target when investigating a focused failure:

```bash
cmake --build --preset static-analysis --target clang-tidy
cmake --build --preset static-analysis --target clang-format-check
```

`clang-tidy` reads the root `.clang-tidy` file, uses the static-analysis compilation database, and filters diagnostics to GameWIP-owned source and header roots. Diagnostics selected by `.clang-tidy` are errors. Suppress a diagnostic only at the narrowest justified location and include a short reason in the `NOLINT` comment.

Headers are analyzed when they are included by a compiled translation unit. Public headers should also have matching validation translation units under `game/validation/public_headers/` so the header can be checked as an include boundary instead of only through incidental implementation includes.

Win32 resource scripts are compiled by the Windows resource compiler and are intentionally not passed to clang-tidy. The root manifest and generated CMake inputs are validated by the normal configure and build path.

## Formatting fixes

The static-analysis target checks formatting but does not rewrite files. Use this command from an MSYS2 UCRT64 shell to format maintained C++ files locally:

```bash
git ls-files -z -- \
  'foundation/*.cpp' 'foundation/*.h' 'foundation/*.hpp' 'foundation/*.inl' \
  'tools/*.cpp' 'tools/*.h' 'tools/*.hpp' 'tools/*.inl' \
  'engine/*.cpp' 'engine/*.h' 'engine/*.hpp' 'engine/*.inl' \
  'game/*.cpp' 'game/*.h' 'game/*.hpp' 'game/*.inl' \
| xargs -0 clang-format -i --style=file
```

Review the diff before committing formatter output:

```bash
git diff -- foundation tools engine game
```

Do not run project formatting over `external/`, generated build trees, generated documentation output, or other third-party or generated artifacts.

## Repository checks

The `Validation / Repository Checks` GitHub job runs checks that do not belong to clang:

- Node.js syntax checks and unit tests for repository automation JavaScript.
- Python syntax validation for repository maintenance scripts.
- JSON parsing for maintained JSON files.
- actionlint validation for GitHub Actions workflows.
- Local relative Markdown link validation for maintained documentation.

Run the script and documentation checks locally from the repository root:

```bash
python -c "from pathlib import Path; p = Path('.github/scripts/check-markdown-links.py'); compile(p.read_text(encoding='utf-8'), str(p), 'exec')"
python .github/scripts/check-markdown-links.py
git diff --check
```

The explicit `compile(...)` syntax check avoids writing Python bytecode caches under `.github/scripts`.

Run JavaScript automation checks when changing repository or release automation:

```bash
node --check .github/scripts/project-automation.js
node --check .github/scripts/project-automation.test.js
node --test .github/scripts/project-automation.test.js
node --check .github/scripts/release-preparation.js
node --check .github/scripts/release-preparation.test.js
node --test .github/scripts/release-preparation.test.js
```

Run workflow linting when changing GitHub Actions. CI runs actionlint through a container; local usage requires actionlint to be installed or run through an equivalent container command:

```bash
actionlint
```

## Documentation checks

The regular validation workflow builds Doxygen and rejects Doxygen warnings. Markdown registered with Doxygen is therefore parsed and cross-reference checked as part of documentation validation.

The repository link checker validates local relative links in maintained Markdown entry points, generated-manual source pages, and GitHub documentation files. It ignores external URLs and `#anchor` fragments; Doxygen-owned anchors and API cross-references are validated by the documentation build.

When documentation, public headers, Doxygen registration, or manual pages change, also run the documentation preset:

```bash
cmake --preset docs
cmake --build --preset docs
```

Then perform the warning-log check in @ref project_documentation. A successful Doxygen process exit alone does not prove that the generated manual is warning-free.

Doxygen validates syntax and links, but it does not judge prose consistency. First-party Markdown must also be reviewed against the heading, voice, terminology, list, example, and ownership rules in @ref project_documentation.

## Local pre-commit checklist

Use this focused checklist before committing documentation, validation, or C++ standardization work:

```bash
python -c "from pathlib import Path; p = Path('.github/scripts/check-markdown-links.py'); compile(p.read_text(encoding='utf-8'), str(p), 'exec')"
python .github/scripts/check-markdown-links.py
git diff --check
cmake --preset static-analysis
cmake --build --preset static-analysis
```

Add `cmake --preset docs` and `cmake --build --preset docs` when the change touches public comments, Doxygen pages, library docs, or documentation registration.

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
| clang-tidy cannot find `compile_commands.json`. | The static-analysis preset was not configured, or the command is running against the wrong build tree. | Run `cmake --preset static-analysis` from the MSYS2 UCRT64 environment. |
| clang-tidy fails on a new diagnostic. | The code violates an enabled check. | Fix the code or add a narrow justified suppression. |
| clang-format check fails. | Maintained C++ formatting differs from `.clang-format`. | Run the formatter locally, review the diff, and commit the result. |
| A public header is not checked directly. | No validation translation unit includes it as a public boundary. | Add or update the matching file under `game/validation/public_headers/`. |
| Python script validation fails. | A repository maintenance script contains invalid syntax for the configured Python. | Fix the script before running repository checks again. |
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
