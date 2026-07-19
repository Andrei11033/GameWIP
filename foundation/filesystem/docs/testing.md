@page filesystem_testing Maintainer validation

@note This page describes proof coverage and environment requirements, not installed consumer API.

## Focused validation

The FileSystem module covers:

- option/result defaults and move-only resource ownership;
- predicates, metadata, path operations, canonicalization, and UTF-8 conversion;
- whole-file helpers and explicit handles, including partial progress;
- create, list, resize, copy, move, remove, and tree-removal limits;
- open modes, sharing, replacement, append, and flush behavior;
- atomic replacement, prefix validation, durability requests, and cleanup;
- shared/exclusive lock acquisition, contention, detached ownership, failed unlock, and destructor cleanup.

Run the FileSystem-focused module through the project validation workflow documented by @ref project_testing.

## Symlink and backend coverage

Scenarios validate `DoNotFollow`, `FollowFinal`, and `FollowAll` for final and intermediate links. A host without symbolic-link creation capability records skips rather than failing unrelated coverage. Complete Windows proof therefore requires Developer Mode or create-symbolic-link privilege.

Backend tests cover native path conversion, strict traversal, sharing, read-only metadata, directory cursors, lock ownership, directory flushing, and native error mapping.

## Other validation boundaries

Project validation also checks:

- `filesystem/filesystem.h` as a self-contained public header;
- exact-version installed-package consumption through `GameWIP::FileSystem`;
- integration with Logger's file output and other consumers;
- Doxygen warnings and page references.

## Test hooks

Use @ref filesystem_test_hooks for the source-tree-only failed-unlock hook and reset protocol.

## Documentation validation

The Doxygen warning log must be empty. Manual pages document portable observable behavior; backend-specific mechanics belong in internal source comments unless they define a consumer-visible constraint.

See @ref project_documentation and @ref project_testing.
