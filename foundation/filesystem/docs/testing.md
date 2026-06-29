@page foundation_filesystem_testing FileSystem testing

FileSystem validation is split across compile-time API checks, normal runtime tests, symlink-policy tests, platform behavior checks, and generated documentation inspection.

## Normal tests

The FileSystem test suite covers:

- public result and option types;
- default option values;
- move-only handle and lock ownership;
- entry predicates and metadata queries;
- UTF-8 path conversion;
- whole-file helpers and explicit handles;
- directory creation, listing, and removal;
- mutation, copy, move, and removal helpers;
- atomic write replacement;
- whole-file lock acquisition and contention.

## Symlink policy tests

Symlink tests validate `DoNotFollow`, `FollowFinal`, and `FollowAll` behavior for final file symlinks and intermediate directory symlinks.

On hosts where symlink creation is unavailable, the affected checks are skipped by the test suite instead of failing unrelated validation.

## Atomic write and durability checks

Atomic write tests verify visible content replacement and fail-if-exists behavior. The public contract also requires the generated docs to describe that there is no non-atomic fallback, temporary files use restrictive access, and `flushParentDirectory` is a real durability request rather than a best-effort hint.

## Platform backend checks

FileSystem has a platform backend. Backend validation should cover native path conversion, strict symlink-policy traversal, sharing behavior, lock behavior, read-only metadata, directory flushing, and native error mapping.

Internal backend contracts stay in source comments and private project notes rather than generated user docs. Public docs describe observable behavior and portable failure statuses.

## Documentation validation

Doxygen generation is part of FileSystem validation because the public API is large enough that the generated manual is the consumer-facing contract.

The warning log should be empty before treating the generated FileSystem manual as validated.

GameWIP owns the test executable, focused-module command, CTest registration, documentation preset, report location, and generated-output paths. See @ref library_testing and @ref library_documentation.
