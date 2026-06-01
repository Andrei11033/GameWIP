# GameWIP Libraries

GameWIP uses small, focused C++20 libraries for foundation-level diagnostics and test support. This site is the user-facing manual plus API reference for those libraries.

## Libraries

- @subpage logger
- @subpage assert
- @subpage test_support

## Project references

- @subpage gamewip_build
- @subpage gamewip_testing
- @subpage gamewip_coverage
- @subpage gamewip_documentation
- @subpage doxygen_notes

## Documentation model

The generated site combines compact public-header API reference with Markdown manual pages. Header comments are optimized for IntelliSense and quick use; the Markdown pages are the full manual with concepts, examples, edge cases, and usage guidance. Each library landing page separates user-manual pages from developer-validation pages where relevant.

Private `.txt` notes under `docs/` are development checklists and are intentionally not included in Doxygen.
