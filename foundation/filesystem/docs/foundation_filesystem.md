@page foundation_filesystem FileSystem

`GameWIP::FileSystem` is the public contract and implementation boundary for local filesystem access.

Include `filesystem/filesystem.h`. Passive values live under `GameWIP::FileSystem::Types`; active handles and operations live directly under `GameWIP::FileSystem`.

## Purpose

FileSystem replaces application use of:

- direct `std::filesystem` operations, except construction and storage through `Types::Path`;
- `std::fstream` and `FILE*`;
- platform-specific file, directory, sharing, and locking APIs.

It owns paths, files, directories, metadata, copy, move, removal, atomic writes, open-time sharing, and whole-file locks.

It does not own watchers, recursive copy, parsing, serialization, archives, virtual filesystems, compression, encryption, asset loading, or higher-level save/cache systems.

## API shape

Persistent native state uses move-constructible, non-copyable objects:

```cpp
GameWIP::FileSystem::FileReader reader;
const auto openStatus = reader.open(path);
const auto readResult = reader.read(buffer);
const auto lockResult = reader.tryLockShared();
const auto closeStatus = reader.close();
```

Path operations and one-shot helpers remain free functions:

```cpp
const auto bytes = GameWIP::FileSystem::readAllBytes(path);
const auto exists = GameWIP::FileSystem::exists(path);
const auto copyStatus = GameWIP::FileSystem::copyFile(from, to);
```

Whole-file helpers internally open, operate, and close. Explicit objects are needed for repeated transfers, seeking, custom sharing, locking, or controlled handle lifetime.

Move assignment is intentionally deleted for file handles and locks. Replacing an active destination would require an implicit close, flush, or unlock whose failure could not be reported by ordinary C++ move assignment. Close or unlock explicitly, then move-construct the next owner.

Non-atomic write and append helpers return `IO::Types::WriteResult` so failures retain accepted payload progress. Atomic helpers return `IO::Types::Status` because their contract is complete path replacement rather than externally useful temporary-file progress.

## Failure model

Expected failures use `IO::Types::Status`; public operations are `noexcept`. Internal allocation and standard-library exceptions must be converted to statuses.

Predicate queries return successful `false` for missing paths. Value queries such as `getEntryInfo()`, `getFileSize()`, and `getLastWriteTime()` return `NotFound`.

Empty target paths, invalid option combinations, and unknown enum values return `InvalidArgument`. Native path rules remain platform-specific; FileSystem does not invent a cross-platform filename grammar.

`SymlinkPolicy::DoNotFollow` and `FollowFinal` are security-sensitive contracts. Implementations must reject intermediate symlinks during handle-relative traversal rather than checking a path and reopening it later. A backend that cannot enforce the requested policy without a path-replacement race returns `Unsupported`.

Path-opening and ordinary content-mutation helpers default to `FollowAll`, matching normal filesystem access. Entry-oriented queries, listing, move, copy, and removal retain operation-specific no-follow defaults where acting on the directory entry itself is important.

`Types::FileTime` uses `std::chrono::system_clock::time_point`. Backends preserve the closest representable native value; precision may be reduced and unrepresentable native timestamps return `SizeLimitExceeded`.

Filesystem operations may block on storage, sharing, locks, or operating-system metadata work. Different handle objects may be used concurrently, but the same handle or lock object is not internally synchronized. Free path operations are independently callable from multiple threads; process-wide current-directory changes still affect the whole process.

See @ref foundation_filesystem_file_open_modes, @ref foundation_filesystem_atomic_write, @ref foundation_filesystem_directory_operations, and @ref foundation_filesystem_unicode_paths.
