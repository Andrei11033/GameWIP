@page doxygen_notes Documentation notes

This page records how the generated Doxygen documentation is intended to be used and extended.

## Documentation split

GameWIP uses three documentation layers:

- **Public header comments** are compact IntelliSense/API-reference contracts. They explain what a type/function/macro does, parameters, return values, lifecycle rules, thread-safety, blocking behavior, failure behavior, and the most important performance notes.
- **Doxygen Markdown pages** are the full how-to manual. Project-wide Doxygen pages live under `docs/doxygen/`; library-owned manual pages stay beside each library under its local `docs/` folder, such as `foundation/io/docs/` or `tools/logger/docs/`.
- **Implementation comments** are only for non-obvious internals such as locks, atomics, ownership, shutdown invariants, platform assumptions, rare failure paths, and test-hook behavior.
- **Private `.txt` notes** under `docs/` are checklists and planning notes. They are not Doxygen inputs.

Headers should not become long tutorials. If the explanation is too long for IntelliSense, put it in the library docs folder and link to it.

## Library-owned docs

Each library owns its own `docs/` folder. The only required page is the library landing page, for example:

```text
tools/logger/docs/logger.md
tools/debug/assert/docs/assert.md
foundation/io/docs/io.md
foundation/terminal/docs/terminal.md
```

The library chooses the rest of its pages. Common user-manual pages are `quick_start.md`, `public_api.md`, `examples.md`, and `troubleshooting.md`. Developer-only validation pages such as `testing.md` and `test_hooks.md` may exist, but they should be labeled clearly and kept separate from the normal user manual navigation.

Every page should use a stable lowercase snake_case Doxygen page id. Child page ids should be prefixed by the library id, for example `logger_quick_start` or `assert_macro_behavior`.

## CMake registration

Project-wide Doxygen helpers live in `cmake/GameWIPDoxygen.cmake`. Library-specific CMake helpers should live in that library's own `cmake/` folder.

Project-level Doxygen files live under `docs/doxygen/`, including the root landing page, project build/testing/coverage pages, `Doxyfile.in`, and Doxygen CSS. The parent `docs/` folder is reserved for private project `.txt` notes.

Libraries should register their public headers and docs with:

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

The root build creates one unified `docs` target. Normal builds do not require Doxygen.

The generated Doxyfile must use explicit inputs, `RECURSIVE = NO`, public headers, and Markdown pages only. It must not include `.txt` notes, `.cpp` implementation files, build folders, external dependencies, generated HTML, or internal test-hook headers as public API.

## Navigation style

The root page links only to major sections, such as IO, Terminal, Logger, Assert, and TestSupport. Each library landing page owns its child page list with `@subpage` links. This keeps the sidebar useful instead of flattening every guide page under the root page.

## Test hooks

Test hooks may be documented because they are important for validation, coverage, and rare-path tests. They must be clearly labeled developer-validation-only and must not be presented as normal production public API. Library landing pages should prefer user-facing manual pages first, then put test hooks and validation pages in a separate developer section.
