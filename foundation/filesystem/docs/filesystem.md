@page filesystem FileSystem

`GameWIP::FileSystem` is the project boundary for local paths, files, directories, metadata, replacement, sharing, and whole-file locking.

Include `filesystem/filesystem.h`. Passive values live in `GameWIP::FileSystem::Types`; resource owners and operations live directly in `GameWIP::FileSystem`.

## Consumer manual

- @subpage filesystem_quick_start
- @subpage filesystem_public_api
- @subpage filesystem_whole_file_io
- @subpage filesystem_file_open_modes
- @subpage filesystem_path_operations
- @subpage filesystem_unicode_paths
- @subpage filesystem_symlink_policies
- @subpage filesystem_metadata
- @subpage filesystem_directory_operations
- @subpage filesystem_atomic_write
- @subpage filesystem_examples
- @subpage filesystem_troubleshooting

## Maintainer validation

- @subpage filesystem_testing
- @subpage filesystem_test_hooks

## Generated API reference

Use @ref GameWIP::FileSystem for resource owners and operations, and @ref GameWIP::FileSystem::Types for paths, enums, options, metadata, and results. The generated reference owns exact signatures and field declarations; the manual explains how the pieces compose and which observable behavior callers may rely on.

## Key behavior

- Expected failures are returned through `GameWIP::IO` statuses and results; public operations are `noexcept`.
- Whole-file helpers own their open, transfer, optional flush, and close sequence. Explicit handles are for repeated transfers, seeking, custom sharing, locking, or controlled lifetime.
- Non-atomic write and append helpers preserve accepted payload progress. Atomic helpers expose only the final path-replacement result.
- Operations that accept `SymlinkPolicy` default to `DoNotFollow`. Requests that cannot be enforced without weakening the selected traversal contract return `Unsupported`.
- Text helpers store bytes unchanged. Path-text conversion is explicit through `pathFromUtf8()` and `pathToUtf8()`.
- File operations may block on storage, sharing, locks, metadata, traversal, or durability work. One handle or lock object is not internally synchronized.
- `setCurrentDirectory()` mutates process-wide state and affects relative path resolution in every thread.

## Dependency boundary

FileSystem is installed as the static target `GameWIP::FileSystem` and publicly depends on `GameWIP::IO`. The installed package exports `filesystem/filesystem.h`; internal and platform headers are not consumer API.

The public interface exposes C++ standard-library path, string, vector, span, chrono, and smart-pointer-backed types. Consumers therefore follow the project compiler, standard-library, runtime, and exact-version compatibility policy described by @ref project_library_compatibility.

FileSystem does not own watchers, recursive copy, archives, virtual filesystems, parsing, serialization, compression, encryption, asset loading, or higher-level save/cache policy.
