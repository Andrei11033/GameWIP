# GameWIP Developer Documentation

This site separates reusable library manuals from GameWIP project integration. Library pages explain their owning API and behavior. Project pages explain how GameWIP builds, combines, validates, and ships those libraries.

## Foundation libraries

- @subpage io
- @subpage terminal
- @subpage foundation_filesystem

## Tool libraries

- @subpage logger
- @subpage assert
- @subpage test_support

## Project build and integration

- @subpage library_build
- @subpage project_validation

## Project quality workflows

- @subpage library_testing
- @subpage project_benchmarking
- @subpage library_coverage
- @subpage project_static_analysis
- @subpage project_repository_automation

## Documentation system

- @subpage library_documentation
- @subpage doxygen_notes

## Documentation model

The generated site combines compact public-header API reference with Markdown manual pages. Header comments are optimized for IntelliSense and quick use; library Markdown pages are the full API manuals. Project Markdown pages own repository-level behavior such as presets, startup sequencing, modular validation, report locations, CI expectations, and documentation generation. Each library landing page separates user-manual pages from developer-validation pages.

Private `.txt` notes under `docs/` are development checklists and are intentionally not included in Doxygen.
