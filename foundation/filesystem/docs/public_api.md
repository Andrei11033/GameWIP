@page filesystem_public_api FileSystem public API

Include `filesystem/filesystem.h`. Installed consumers link `GameWIP::FileSystem`; source-tree consumers link `FileSystem`.

## Constants and aliases

| API | Contract |
| --- | --- |
| `kAtomicTemporaryNamePrefix` | Default same-directory temporary filename prefix used by atomic writes. |
| `kNoEntryLimit` | Sentinel meaning no caller-imposed listing or tree-removal entry limit. Backend and container limits still apply. |
| `Types::Path` | Alias for `std::filesystem::path`; it is not a portable path-grammar abstraction. |
| `Types::FileTime` | Alias for `std::chrono::system_clock::time_point`; native precision may be reduced. |

## Enum types

### Entry, access, and open policy

- `EntryKind`: `RegularFile`, `Directory`, `Symlink`, `Other`.
- `FileAccess`: `Read`, `Write`, `ReadWrite`.
- `FileOpenMode`: `OpenExisting`, `CreateNew`, `OpenOrCreate`, `TruncateExisting`, `CreateOrTruncate`.
- `FileInitialPosition`: `Beginning`, `End`.
- `FileWriterMode`: `CreateNew`, `CreateOrTruncate`, `TruncateExisting`, `OpenOrCreate`, `AppendOrCreate`, `AppendExisting`.
- `WriteFileMode`: `CreateNew`, `CreateOrTruncate`, `TruncateExisting`.
- `AppendMode`: `AppendOrCreate`, `AppendExisting`.
- `ReplaceMode`: `FailIfExists`, `ReplaceExisting`.

`FileInitialPosition::End` is one initial seek. It does not provide append semantics.

### Sharing, links, locks, and metadata

- `FileShare`: `None`, `Read`, `Write`, `Delete`, `ReadWrite`, `All`. The `operator|`, `operator&`, `operator|=`, and `operator&=` overloads combine and test masks.
- `SymlinkPolicy`: `DoNotFollow`, `FollowFinal`, `FollowAll`.
- `FileLockMode`: `Shared`, `Exclusive`.
- `LockOutcome`: `Acquired`, `WouldBlock`.
- `CopyMetadataMode`: `None`, `Basic`.

See @ref filesystem_file_open_modes and @ref filesystem_symlink_policies for operational meaning.

## Option structures

### Handle and whole-file I/O

- `FileOpenOptions`: `access`, `mode`, `initialPosition`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushOnClose`.
- `FileReaderOpenOptions`: `share`, `symlinkPolicy`.
- `FileWriterOpenOptions`: `mode`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushOnClose`.
- `ReadFileOptions`: nested reader `open`, hard `maxBytes`, and transfer `bufferSize`.
- `WriteFileOptions`: `mode`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushMode`.
- `AppendFileOptions`: `mode`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushMode`.
- `AtomicWriteOptions`: `createParentDirectories`, `replaceMode`, `symlinkPolicy`, `flushMode`, `flushParentDirectory`, owning `temporaryNamePrefix`.

### Queries, directories, copy, move, and removal

- `QueryOptions`: query `symlinkPolicy`.
- `MutationOptions`: resize/truncate `symlinkPolicy`.
- `CreateDirectoryOptions`: `succeedIfAlreadyExists`, `symlinkPolicy`.
- `ListDirectoryOptions`: `includeFiles`, `includeDirectories`, `includeSymlinks`, `includeOther`, `includeHidden`, child-metadata `symlinkPolicy`, and `maxEntries`.
- `RemoveOptions`: `succeedIfMissing`, `symlinkPolicy`.
- `RemoveDirectoryTreeOptions`: `succeedIfMissing`, initial-path `symlinkPolicy`, `maxEntries`.
- `MoveOptions`: `replaceMode`, source `symlinkPolicy`, `createParentDirectories`.
- `CopyFileOptions`: `replaceMode`, source `symlinkPolicy`, `createParentDirectories`, `metadataMode`, `flushMode`.

Unknown enum values and invalid option combinations return `InvalidArgument`.

## Metadata and result structures

- `EntryInfo`: `kind`, optional `sizeBytes`/`hasSize`, optional `lastWriteTime`/`hasLastWriteTime`, and portable `readOnly` state.
- `BoolResult`: `status`, `value`.
- `EntryInfoResult`: `status`, `info`.
- `LastWriteTimeResult`: `status`, `time`.
- `PathResult`: `status`, `path`.
- `Utf8PathResult`: `status`, `utf8`.
- `DirectoryEntry`: child `path` and `info`. The path is the supplied parent path joined with the child name; it is not necessarily absolute.
- `ListDirectoryResult`: `status`, collected `entries`.
- `RemoveDirectoryTreeResult`: `status`, completed `removedEntries`.
- `LockResult`: `status`, `outcome`, and active `lock` only when acquired.

A failed status can coexist with meaningful progress for listing and tree removal. Payload values in ordinary query results are meaningful only when their status is successful.

## Resource owners

### `FileReader`

Read-only `IO::Reader` with `open()`, `isOpen()`, `canSeek()`, `read()`, `close()`, `position()`, `size()`, `seek()`, and `tryLockShared()`.

### `FileWriter`

Write-only `IO::Writer` with `open()`, `isOpen()`, `canSeek()`, `write()`, `flush()`, `close()`, `position()`, `size()`, `seek()`, and `tryLockExclusive()`. Append modes are non-seekable.

### `File`

Read/write `IO::Reader` and `IO::Writer` with the common stream operations plus `access()`, `resize()`, and shared/exclusive lock acquisition. `access()` is meaningful only while open.

### `FileLock`

Move-only unlock owner with `active()`, `mode()`, and retryable `unlock()`. A lock owns independent native state and can remain active after the object from which it was acquired is destroyed.

All four classes are move-constructible, non-copyable, and deliberately not move-assignable. See @ref filesystem_file_open_modes.

## Whole-file operation families

- Read: `readAllBytes()`, `readAllText()`.
- Exact non-atomic write: span/vector `writeAllBytes()`, `writeAllText()`.
- Append: span/vector `appendBytes()`, `appendText()`.
- Atomic replacement: span/vector `writeAllBytesAtomic()`, `writeAllTextAtomic()`.

The vector overloads forward to the corresponding span overload without changing semantics. See @ref filesystem_whole_file_io and @ref filesystem_atomic_write.

## Metadata and mutation families

- Predicates: `exists()`, `isRegularFile()`, `isDirectory()`, `isSymlink()`.
- Value queries: `getEntryInfo()`, `getFileSize()`, `getLastWriteTime()`, `isReadOnly()`.
- Metadata mutation: `setReadOnly()`.
- Size mutation: `resizeFile()`, `truncateFile()`, and `File::resize()`.

See @ref filesystem_metadata.

## Directory and path mutation families

- Creation: `createDirectory()`, `createDirectories()`.
- Enumeration: `listDirectory()`.
- Copy and move: `copyFile()`, `movePath()`.
- Removal: `removeFile()`, `removeEmptyDirectory()`, `removeDirectoryTree()`.

See @ref filesystem_directory_operations and @ref filesystem_symlink_policies.

## Path operation families

- Process directory: `getCurrentDirectory()`, `setCurrentDirectory()`.
- Lexical components: `parentPath()`, `filename()`, `stem()`, `extension()`, `replaceExtension()`, `joinPath()`.
- Lexical classification: `isAbsolutePath()`, `isRelativePath()`.
- Filesystem resolution: `absolutePath()`, `canonicalPath()`, `weaklyCanonicalPath()`, `getTemporaryDirectoryPath()`.
- UTF-8 boundary: `pathFromUtf8()`, `pathToUtf8()`.

See @ref filesystem_path_operations and @ref filesystem_unicode_paths.

## Failure, exceptions, blocking, and threading

Every public FileSystem operation is `noexcept`. Allocation and standard-library exceptions are translated to `OutOfMemory`, `EncodingFailed`, `InvalidArgument`, or `Unknown` according to the operation. Expected native failures use the portable IO error model and may include native diagnostic data.

Operations may block on storage, sharing, locks, metadata, traversal, or flush work. Different handle objects and free operations may be called concurrently. The same handle or lock object is not internally synchronized. Current-directory mutation remains process-wide.

## Package and binary boundary

FileSystem is a static library. Its installed package exports `GameWIP::FileSystem`, installs `filesystem/filesystem.h`, requires the exact matching IO package, and has no generated export header.

Public inheritance from IO interfaces and exposure of standard-library types require compatible compiler, standard-library, runtime, and build settings. Internal declarations under `filesystem/internal` and platform code are not installed API.
