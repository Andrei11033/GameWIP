# GameWIP Foundation, Tools, and Validation

This site documents the C++23 foundation libraries, diagnostics, build modes, and modular validation system used by GameWIP.

## Libraries

- @subpage foundation_filesystem
- @subpage io
- @subpage terminal
- @subpage logger
- @subpage assert
- @subpage test_support

## Project references

- @subpage library_build
- @subpage project_validation
- @subpage library_testing
- @subpage project_benchmarking
- @subpage library_coverage
- @subpage library_documentation
- @subpage doxygen_notes

## Documentation model

The generated site combines compact public-header API reference with Markdown manual pages. Header comments are optimized for IntelliSense and quick use; the Markdown pages are the full manual with concepts, examples, edge cases, and usage guidance. Each library landing page separates user-manual pages from developer-validation pages where relevant.

Private `.txt` notes under `docs/` are development checklists and are intentionally not included in Doxygen.
