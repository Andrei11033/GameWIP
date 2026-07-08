@page project_static_analysis Static analysis and repository checks

GameWIP applies format-specific checks to maintained project files. Third-party code under
`external/` and generated files under `build/` are not project-owned and are excluded.

## C++ checks

The `static-analysis` preset configures a compilation database and enables two targets:

- `clang-tidy` analyzes C and C++ translation units under `foundation/`, `tools/`, `engine/`,
  and `game/`. Headers in those roots are checked when included by a translation unit.
- `clang-format-check` verifies `.cpp`, `.h`, `.hpp`, and `.inl` files under the same roots
  without modifying them.

Run both checks with:

```powershell
cmake --preset static-analysis
cmake --build --preset static-analysis
```

The MSYS2 UCRT64 environment must provide `clang`, `clang-tools-extra`, `cmake`, `ninja`, and
`python`. Diagnostics selected in the root `.clang-tidy` file are errors. Suppress a diagnostic
only at the narrowest justified location and include a short reason in the `NOLINT` comment.

Win32 resource scripts are compiled by the Windows resource compiler and are intentionally not
passed to clang-tidy. The root manifest and generated CMake inputs are validated by the normal
configure and build path.

## Other maintained formats

The `Validation / Repository Checks` GitHub job runs the checks that do not belong to clang:

- Node.js syntax checks and unit tests for repository automation JavaScript
- JSON parsing for maintained JSON files
- actionlint validation for every GitHub Actions workflow

The regular validation jobs also configure all project CMake files, run modular correctness tests,
dry-run benchmark registrations, build Doxygen, and reject Doxygen warnings. Markdown registered
with Doxygen is therefore validated as part of the documentation build.

Doxygen validates parsing and generated cross-references, but it does not judge prose consistency. First-party Markdown must also be reviewed against the heading, voice, terminology, list, example, and ownership rules in @ref project_documentation. The project verification pass checks relative Markdown links separately; `external/` and generated build output remain outside this editorial scope.

The matching local commands are:

```powershell
node --check .github/scripts/project-automation.js
node --check .github/scripts/project-automation.test.js
node --test .github/scripts/project-automation.test.js
actionlint
```

VS Code exposes `Run Static Analysis` as a workspace task.
