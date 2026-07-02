@page project_documentation Documentation system

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
- Repository Markdown under `docs/` is not a Doxygen input unless it is registered explicitly.
- Internal and platform backend contracts stay in source comments, not consumer-manual pages.
- Library docs own their public API, behavior, examples, troubleshooting, and a clearly separated maintainer-validation appendix.
- Root project pages own GameWIP integration: presets, startup flow, module discovery, report locations, CI, packaging, and repository-wide policy.
- Shared facts have one authoritative page; other pages link to it instead of copying a second contract.

## Registering a library

A library CMake file registers its public headers and docs folder like this:

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

The helper name is repository infrastructure, not a consumer-facing CMake API. Installed packages expose only their exported `GameWIP::` targets and package configuration files.

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

## Consumer manual versus maintainer validation

The generated Doxygen site is primarily a professional user manual for the reusable libraries. A user should be able to learn how to use the public API from the generated docs without reading `.cpp` files.

Library landing pages split navigation into:

- **User manual** pages: quick start, public API guide, concepts, configuration, examples, troubleshooting, and other pages needed by normal consumers of the library.
- **Maintainer validation** pages: coverage summaries, manual validation procedures, and exact use of gated internal hooks. These pages are visibly non-consumer material and must state that hooks are source-tree-only, non-installed interfaces.

Every public API needs concise IntelliSense documentation in its public header. APIs with non-trivial behavior also need Markdown coverage in the library manual explaining when to use them, lifecycle/threading rules, failure behavior, blocking behavior, performance expectations, and examples. The manual must account for every public namespace, type, enum value, field, function, macro, and constant, while allowing genuinely related overloads to share one behavior table.

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

Consumer Markdown pages are for the full supported manual:

- quick starts,
- API family and overload behavior tables,
- recipes,
- examples for every public function/macro,
- common mistakes,
- troubleshooting,

Maintainer validation pages may document hook enablement, internal include paths, reset rules, and proof coverage. They do not explain unrelated private implementation mechanics.

Internal headers and implementation files are maintainer documentation. Give every named internal namespace, type, function, and non-obvious constant a concise purpose comment. Add ownership, locking, state-transition, platform, fallback, units, or performance details when the signature cannot communicate them safely. Do not narrate obvious control flow, assignments, accessors, or local variables. These source comments remain outside the generated public manual.

## Test-hook documentation

Test hooks are supported for source-tree maintainers and documented in labeled maintainer pages because they matter for deterministic validation. Hook headers are not installed or versioned as consumer API. External installed-package consumers validate only supported public behavior.

## Doxygen verification

Documentation generation is opt-in through `GAMEWIP_BUILD_DOCS`. Normal builds must not require Doxygen. The generated Doxyfile should keep `RECURSIVE = NO`, list explicit inputs, write HTML under the build tree, and write warnings to `build-docs/docs/doxygen/doxygen_warnings.log`.

Planned library docs may exist before a library target is implemented. Those docs should state that generated Doxygen registration is still to be implemented and should not be listed on the root generated index until registration exists.

See @ref project_extending for the complete library and documentation checklists.
