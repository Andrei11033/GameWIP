@page filesystem_testing FileSystem maintainer validation

@note This page is for maintainers. It describes proof coverage and environment requirements, not supported consumer API.

## Compile-time and normal coverage

The focused FileSystem suite covers:

- result and option types, default values, and move-only handle/lock ownership;
- entry predicates, metadata, canonicalization, and UTF-8 path conversion;
- whole-file helpers and explicit reader/writer handles;
- creation, enumeration, mutation, copy, move, and removal;
- open/share/replace policies and partial IO progress;
- atomic replacement, invalid temporary prefixes, flush requests, and cleanup;
- shared/exclusive lock acquisition, contention, explicit unlock, and cleanup.

## Symlink and backend coverage

Symlink scenarios validate `DoNotFollow`, `FollowFinal`, and `FollowAll` for final and intermediate links. A host that cannot create symlinks records skips instead of failing unrelated validation. Complete proof therefore requires a Windows account with Developer Mode or create-symbolic-link privilege.

Backend coverage exercises native path conversion, strict traversal, sharing, read-only metadata, lock behavior, directory flushing, and native error mapping. Internal backend declarations are documented in source comments rather than the consumer manual.

## Test hooks

@warning These hooks are supported source-tree maintainer interfaces. They are not installed and are not versioned consumer API.

The GameWIP `validation` preset enables `FILESYSTEM_ENABLE_TEST_HOOKS`. Source-tree tests that link the short `FileSystem` target may include `filesystem/internal/filesystem_platform.h` and use its gated `Detail::Platform::TestHooks` declarations. The build-tree target supplies `INTERNAL_FILESYSTEM_TEST_HOOKS=1`; installed packages intentionally do not expose this internal contract.

## Documentation coverage

Doxygen is part of validation because the public surface is large. The warning log must be empty, and generated pages must describe portable observable behavior rather than Win32 implementation mechanics.

GameWIP owns module selection, CTest registration, reports, and generated output. See @ref project_testing and @ref project_documentation.
