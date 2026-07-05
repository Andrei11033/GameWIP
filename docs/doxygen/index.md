# GameWIP Developer Documentation

This site separates reusable library manuals from GameWIP project integration. Library pages explain their owning API and behavior. Project pages explain how GameWIP builds, combines, validates, and ships those libraries.

## Foundation libraries

- @subpage io
- @subpage terminal
- @subpage filesystem

## Tool libraries

- @subpage logger
- @subpage assert
- @subpage test_support

## Project build and integration

- @subpage project_structure
- @subpage project_build
- @subpage project_library_compatibility
- @subpage project_validation
- @subpage project_extending

## Project quality workflows

- @subpage project_testing
- @subpage project_benchmarking
- @subpage project_coverage
- @subpage project_static_analysis
- @subpage project_repository_automation

## Documentation system

- @subpage project_documentation

## Documentation model

The generated site combines compact public-header API reference with Markdown manual pages. Header comments are optimized for IntelliSense and quick use; library Markdown pages are the full API manuals. Project Markdown pages own repository-level behavior such as presets, startup sequencing, modular validation, report locations, CI expectations, and documentation generation. Each library landing page separates its consumer manual from clearly labeled maintainer validation and internal-hook material.

Repository planning and checklist Markdown under `docs/` is intentionally excluded unless registered as a generated-manual input.
