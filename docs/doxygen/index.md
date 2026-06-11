# Foundation and Tool Libraries

These small, focused C++23 libraries provide foundation-level systems, diagnostics, and test support. This site is their user-facing manual and API reference.

## Libraries

- @subpage foundation_filesystem
- @subpage io
- @subpage terminal
- @subpage logger
- @subpage assert
- @subpage test_support

## Project references

- @subpage library_build
- @subpage library_testing
- @subpage library_coverage
- @subpage library_documentation
- @subpage doxygen_notes

## Documentation model

The generated site combines compact public-header API reference with Markdown manual pages. Header comments are optimized for IntelliSense and quick use; the Markdown pages are the full manual with concepts, examples, edge cases, and usage guidance. Each library landing page separates user-manual pages from developer-validation pages where relevant.

Private `.txt` notes under `docs/` are development checklists and are intentionally not included in Doxygen.
