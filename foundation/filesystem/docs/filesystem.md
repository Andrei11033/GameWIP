@page filesystem FileSystem

`GameWIP::FileSystem` is the project boundary for local paths, files, directories, metadata, replacement, sharing, and whole-file locking.

Include `filesystem/filesystem.h`. Passive values live in `GameWIP::FileSystem::Types`; resource owners and operations live directly in `GameWIP::FileSystem`.

## How the library is organized

Use whole-file helpers for a complete operation with one call. Use explicit
file, directory, and lock owners when work spans several transfers or needs
custom sharing, seeking, flushing, or lifetime control. Path operations define
how names are interpreted; symlink policy defines what may be traversed; status
and result values preserve expected failures without exceptions. Atomic writes
stage new content and replace the destination only after the staged operation
succeeds.

## Consumer manual

- @subpage filesystem_quick_start — Include, link, read, write, and handle a
  failure in a minimal program.
- @subpage filesystem_public_api — Find resource owners, operations, passive
  types, options, and results by capability.
- @subpage filesystem_whole_file_io — Choose between byte/text helpers and
  explicit handles, including size and durability behavior.
- @subpage filesystem_file_open_modes — Understand creation, truncation,
  append, access, sharing, seek, flush, and locking choices.
- @subpage filesystem_path_operations — Compose, normalize, resolve, compare,
  and inspect paths without confusing lexical and filesystem work.
- @subpage filesystem_unicode_paths — Understand UTF-8 conversion and native
  path representation boundaries.
- @subpage filesystem_symlink_policies — Control traversal and understand what
  each operation can enforce safely.
- @subpage filesystem_metadata — Query types, sizes, timestamps, and other
  observable filesystem facts.
- @subpage filesystem_directory_operations — Create, enumerate, and remove
  directories with explicit traversal behavior.
- @subpage filesystem_atomic_write — Replace a file as one visible commit and
  understand staging, durability, and cleanup failures.
- @subpage filesystem_examples — See common file, directory, path, and locking
  workflows in context.
- @subpage filesystem_troubleshooting — Diagnose sharing, permissions,
  traversal, encoding, cleanup, and durability failures.

## Maintainer validation

- @subpage filesystem_testing — See automated behavior, installed-package, and
  platform coverage.
- @subpage filesystem_test_hooks — Understand source-tree-only fault injection
  and mandatory reset behavior.

## Generated API reference

Use @ref GameWIP::FileSystem for resource owners and operations, and @ref
GameWIP::FileSystem::Types for paths, enums, options, metadata, and results. The
generated pages give exact signatures and fields. The guides explain how those
pieces compose and which behavior callers can safely build on.

## Key behavior

- Expected failures are returned through `GameWIP::IO` statuses and results; public operations are `noexcept`.
- Whole-file helpers own their open, transfer, optional flush, and close sequence. Explicit handles are for repeated transfers, seeking, custom sharing, locking, or controlled lifetime.
- Non-atomic write and append helpers preserve accepted payload progress. Atomic helpers expose only the final path-replacement result.
- Operations that accept `SymlinkPolicy` default to `DoNotFollow`. Requests that cannot be enforced without weakening the selected traversal contract return `Unsupported`.
- Text helpers enforce strict UTF-8 without BOM, normalization, or line-ending transformation; byte helpers remain encoding agnostic. Path-text conversion is explicit through `pathFromUtf8()` and `pathToUtf8()`.
- File operations may block on storage, sharing, locks, metadata, traversal, or durability work. One handle or lock object is not internally synchronized.
- `setCurrentDirectory()` mutates process-wide state and affects relative path resolution in every thread.

## Dependency boundary

FileSystem is installed as the static target `GameWIP::FileSystem` and publicly depends on `GameWIP::IO`. It uses `GameWIP::Unicode` privately for FileSystem-owned UTF-8 trust boundaries; the installed package resolves both dependencies for static linking. The installed package exports `filesystem/filesystem.h`; internal and platform headers are not consumer API.

The public interface exposes C++ standard-library path, string, vector, span, chrono, and smart-pointer-backed types. Consumers therefore follow the project compiler, standard-library, runtime, and exact-version compatibility policy described by @ref project_library_compatibility.

FileSystem does not own watchers, recursive copy, archives, virtual filesystems, parsing, serialization, compression, encryption, asset loading, or higher-level save/cache policy.
