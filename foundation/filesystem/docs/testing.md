@page filesystem_testing Testing

@note This page describes proof coverage and environment requirements, not installed consumer API.

## Focused validation

The FileSystem module covers:

- option/result defaults and move-only resource ownership;
- predicates, metadata, path operations, canonicalization, and strict UTF-8 path conversion;
- strict UTF-8 whole-file text helpers, including valid-prefix reads and pre-side-effect write/append/atomic rejection;
- whole-file byte helpers and explicit handles, including partial progress;
- create, list, resize, copy, move, remove, and tree-removal limits;
- open modes, sharing, replacement, append, and flush behavior;
- atomic replacement, prefix validation, durability requests, and cleanup;
- shared/exclusive lock acquisition, contention, detached ownership, failed unlock, and destructor cleanup;
- race-resistant move behavior when a validated parent is renamed or a committed destination is changed concurrently.

Run the FileSystem-focused module through the project validation workflow in
@ref project_testing.

## Symlink and backend coverage

Scenarios validate `DoNotFollow`, `FollowFinal`, and `FollowAll` for final and intermediate links. A host without symbolic-link creation capability
records skips rather than failing unrelated coverage. Complete Windows proof therefore requires Developer Mode or create-symbolic-link privilege.

Backend tests cover native path conversion, strict traversal, sharing, read-only metadata, directory cursors, lock ownership, directory flushing, and
native error mapping.

## Other validation boundaries

Project validation also checks:

- every focused FileSystem header and the umbrella as self-contained headers;
- focused and umbrella installed-package consumption;
- exact-version installed-package consumption through `GameWIP::FileSystem`;
- integration with Logger's file output and other consumers;
- Doxygen warnings and page references.

The failed-unlock scenario forces `UnlockFileEx` to fail during `FileLock` destruction, then verifies that the owning `File` can still clean up and a
competitor can subsequently acquire the lock.

Use @ref filesystem_test_hooks for this focused source-tree-only cleanup seam and the move-coordination seams.

## Documentation validation

The Doxygen warning log must be empty. Manual pages document portable observable behavior; backend-specific mechanics belong in internal source
comments unless they define a consumer-visible constraint.

See @ref project_documentation and @ref project_testing.
