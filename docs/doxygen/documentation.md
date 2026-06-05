@page library_documentation Documentation system

The Doxygen site is designed as a modular manual. Each library owns its own docs and API inputs; the root project only merges them into one unified site.

## Core rules

- One root Doxygen site is generated for all libraries.
- Project-wide Doxygen helper functions live in `cmake/LibraryDoxygen.cmake`.
- Project-wide Doxygen pages, the Doxyfile template, and Doxygen styling live in `docs/doxygen/`.
- Library-specific CMake helper functions live under that library's `cmake/` folder.
- Each generated library owns a library-local `docs/` folder, for example `foundation/io/docs/`, `foundation/terminal/docs/`, or `tools/logger/docs/`.
- Only the library landing page named `<PAGE_ID>.md` is mandatory.
- Any other guide pages are chosen by the library.
- Page IDs use lowercase snake_case.
- Child pages should be prefixed with the library page ID, for example `logger_quick_start` or `assert_macro_behavior`.
- Doxygen inputs are explicit public headers plus Markdown manual pages.
- Private `.txt` files under `docs/` are development notes and are not Doxygen inputs.
- Internal and platform backend contracts stay in private `.txt` notes, not generated user docs.

## Registering a library

A library CMake file registers its public headers and docs folder like this:

```cmake
library_register_doxygen_library(
    NAME Logger
    PAGE_ID logger
    PUBLIC_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/logger.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/logger_macros.h"
    DOCS
        "${CMAKE_CURRENT_SOURCE_DIR}/docs"
)
```

The helper requires a landing page named `<PAGE_ID>.md` in one of the `DOCS` paths. For Logger, that is `logger.md`; for Terminal, that is `terminal.md`.

## Landing pages

A library landing page should be the table of contents for that library:

```markdown
@page <PAGE_ID> <Library name>

- @subpage logger_quick_start
- @subpage logger_examples
- @subpage logger_troubleshooting
```

The root Doxygen page at `docs/doxygen/index.md` should link only to major library/project sections. It should not duplicate every library detail.

## User manual versus developer validation

The generated Doxygen site is primarily a professional user manual for the reusable libraries. A user should be able to learn how to use the public API from the generated docs without reading `.cpp` files.

Library landing pages should therefore split navigation into:

- **User manual** pages: quick start, public API guide, concepts, configuration, examples, troubleshooting, and other pages needed by normal consumers of the library.
- **Developer validation** pages: test-hook notes, coverage notes, manual validation procedures, and internal testing workflows. These pages may be generated, but they should not be presented as normal production API.

Every public API needs concise IntelliSense documentation in its public header. APIs with non-trivial behavior also need Markdown coverage in the library manual explaining when to use them, lifecycle/threading rules, failure behavior, blocking behavior, performance expectations, and examples.

## IntelliSense vs manual pages

Public headers are for compact IntelliSense contracts:

- what the API does,
- parameters and return values,
- lifecycle expectations,
- thread-safety,
- blocking/UI behavior,
- failure behavior,
- performance notes,
- links to deeper manual pages.

Markdown guide pages are for the full manual:

- quick starts,
- API family and overload behavior tables,
- recipes,
- examples for every public function/macro,
- common mistakes,
- troubleshooting,
- test-hook guidance.

Implementation comments should explain non-obvious internals only: locks, atomics, shutdown invariants, platform behavior, hot paths, failure injection, and test hooks.

## Test-hook documentation

Test hooks should be documented in guide pages because they matter for validation, but they must be clearly marked as testing-only/internal. Hook headers are not installed as normal public API.

## Doxygen verification

Documentation generation is opt-in through `BUILD_LIBRARY_DOCS`. Normal builds must not require Doxygen. The generated Doxyfile should keep `RECURSIVE = NO`, list explicit inputs, write HTML under the build tree, and write warnings to `build-docs/docs/doxygen/doxygen_warnings.log`.

Planned library docs may exist before a library target is implemented. Those docs should state that generated Doxygen registration is still to be implemented and should not be listed on the root generated index until registration exists.
