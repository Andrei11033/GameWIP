@page filesystem_public_api Public API

Include `filesystem/filesystem.h` for the complete API. Focused entry points are:

- `filesystem/path.h`: paths, conversion, and path/current-directory operations;
- `filesystem/entry.h`: generic entry metadata, queries, and mutations;
- `filesystem/file.h`: file handles, locks, and whole-file operations;
- `filesystem/directory.h`: directory cursors, listing, creation, and removal.

Focused headers reduce dependencies and clarify intent without changing API
behavior. Installed consumers link `GameWIP::FileSystem`; source-tree consumers
link `FileSystem`.

## Constants and aliases

| API | Contract |
| --- | --- |
| `kAtomicTemporaryNamePrefix` | Default same-directory temporary filename prefix used by atomic writes. |
| `kNoEntryLimit` | Sentinel meaning no caller-imposed listing or tree-removal entry limit. Backend and container limits still apply. |
| `Types::Path` | Alias for `std::filesystem::path`; it is not a portable path-grammar abstraction. |
| `Types::FileTime` | Alias for `std::chrono::system_clock::time_point`; native precision may be reduced. |

`Types` keeps shared entry/path values at its root and groups the larger passive domains under `Types::File`, `Types::Directory`, and `Types::Lock`.
The nested namespace supplies the domain context, so repeated `File`, `Directory`, and `Lock` words are omitted from member type names.

## Enum types

### Entry, access, and open policy

- `EntryKind`: `RegularFile`, `Directory`, `Symlink`, `Other`.
- `Types::File::Access`: `Read`, `Write`, `ReadWrite`.
- `Types::File::OpenMode`: `OpenExisting`, `CreateNew`, `OpenOrCreate`, `TruncateExisting`, `CreateOrTruncate`.
- `Types::File::InitialPosition`: `Beginning`, `End`.
- `Types::File::WriterMode`: `CreateNew`, `CreateOrTruncate`, `TruncateExisting`, `OpenOrCreate`, `AppendOrCreate`, `AppendExisting`.
- `Types::File::WriteMode`: `CreateNew`, `CreateOrTruncate`, `TruncateExisting`.
- `Types::File::AppendMode`: `AppendOrCreate`, `AppendExisting`.
- `ReplaceMode`: `FailIfExists`, `ReplaceExisting`.

`Types::File::InitialPosition::End` is one initial seek. It does not provide append semantics.

### Sharing, links, locks, and metadata

- `Types::File::Share`: `None`, `Read`, `Write`, `Delete`, `ReadWrite`, `All`. Its bitwise operators combine and test masks.
- `SymlinkPolicy`: `DoNotFollow`, `FollowFinal`, `FollowAll`.
- `Types::Lock::Mode`: `Shared`, `Exclusive`.
- `Types::Lock::Outcome`: `Acquired`, `WouldBlock`.
- `Types::File::CopyMetadataMode`: `None`, `Basic`.

See @ref filesystem_file_open_modes and @ref filesystem_symlink_policies for operational meaning.

## Option structures

### Handle and whole-file I/O

- `Types::File::OpenOptions`: `access`, `mode`, `initialPosition`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushOnClose`.
- `Types::File::ReaderOpenOptions`: `share`, `symlinkPolicy`.
- `Types::File::WriterOpenOptions`: `mode`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushOnClose`.
- `Types::File::ReadOptions`: nested reader `open`, hard `maxBytes`, and transfer `bufferSize`.
- `Types::File::WriteOptions`: `mode`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushMode`.
- `Types::File::AppendOptions`: `mode`, `share`, `symlinkPolicy`, `createParentDirectories`, `flushMode`.
- `Types::File::AtomicWriteOptions`: `createParentDirectories`, `replaceMode`, `symlinkPolicy`, `flushMode`, `flushParentDirectory`, owning UTF-8
  `temporaryNamePrefix`.

### Queries, directories, copy, move, and removal

- `EntryOptions`: entry query/metadata `symlinkPolicy`.
- `Types::File::ResizeOptions`: resize/truncate `symlinkPolicy`.
- `Types::Directory::CreateOptions`: `succeedIfAlreadyExists`, `symlinkPolicy`.
- `Types::Directory::ListOptions`: `includeFiles`, `includeDirectories`, `includeSymlinks`, `includeOther`, `includeHidden`, child-metadata
  `symlinkPolicy`, and `maxEntries`.
- `RemoveOptions`: `succeedIfMissing`, `symlinkPolicy`.
- `Types::Directory::RemoveTreeOptions`: `succeedIfMissing`, initial-path `symlinkPolicy`, `maxEntries`.
- `MoveOptions`: `replaceMode`, source `symlinkPolicy`, `createParentDirectories`.
- `Types::File::CopyOptions`: `replaceMode`, source `symlinkPolicy`, `createParentDirectories`, `metadataMode`, `flushMode`.

Unknown enum values and invalid option combinations return `InvalidArgument`.

## Metadata and result structures

- `EntryInfo`: `kind`, optional `sizeBytes`/`hasSize`, optional `lastWriteTime`/`hasLastWriteTime`, and portable `readOnly` state.
- `BoolResult`: `status`, `value`.
- `EntryInfoResult`: `status`, `info`.
- `LastWriteTimeResult`: `status`, `time`.
- `PathResult`: `status`, `path`.
- `Utf8PathResult`: `status`, `utf8`.
- `Types::Directory::Entry`: child `path` and `info`. The path is the supplied parent path joined with the child name; it is not necessarily absolute.
- `Types::Directory::CursorNextResult`: `status`, one `entry`, and `hasEntry`; successful exhaustion has `hasEntry == false`.
- `Types::Directory::ListResult`: `status`, collected `entries`.
- `Types::Directory::RemoveTreeResult`: `status`, completed `removedEntries`.
- `Types::Lock::Result`: `status`, `outcome`, and active `lock` only when acquired.

A failed status can coexist with meaningful progress for listing and tree removal. Payload values in ordinary query results are meaningful only when
their status is successful.

## Resource owners

### `DirectoryCursor`

Move-only, bounded-memory direct-child enumeration with `open()`, `isOpen()`, `next()`, and `close()`. Move assignment closes the destination cursor's
previous enumeration. It applies `Types::Directory::ListOptions` without retaining accepted siblings.

### `FileReader`

Read-only `IO::Reader` with `open()`, `isOpen()`, `canSeek()`, `read()`, `close()`, `position()`, `size()`, `seek()`, and `tryLockShared()`.

### `FileWriter`

Write-only `IO::Writer` with `open()`, `isOpen()`, `canSeek()`, `write()`, `flush()`, `close()`, `position()`, `size()`, `seek()`, and
`tryLockExclusive()`. Append modes are non-seekable.

### `File`

Read/write `IO::Reader` and `IO::Writer` with the common stream operations plus `access()`, `resize()`, and shared/exclusive lock acquisition.
`access()` is meaningful only while open.

### `FileLock`

Move-only unlock owner with `active()`, `mode()`, and retryable `unlock()`. A lock owns independent native state and can remain active after the
object from which it was acquired is destroyed.

The file and lock owners are move-constructible, non-copyable, and deliberately not move-assignable. `DirectoryCursor` is move-constructible and
move-assignable because replacing an enumeration cannot hide a flush, unlock, or close error. See @ref filesystem_file_open_modes.

## Whole-file operation families

- Read: `readAllBytes()`, `readAllText()`.
- Exact non-atomic write: span/vector `writeAllBytes()`, `writeAllText()`.
- Append: span/vector `appendBytes()`, `appendText()`.
- Atomic replacement: span/vector `writeAllBytesAtomic()`, `writeAllTextAtomic()`.

The vector overloads forward to the corresponding span overload without changing semantics. See @ref filesystem_whole_file_io and @ref
filesystem_atomic_write.

## Metadata and mutation families

- Predicates: `exists()`, `isRegularFile()`, `isDirectory()`, `isSymlink()`.
- Value queries: `getEntryInfo()`, `getFileSize()`, `getLastWriteTime()`, `isReadOnly()`.
- Metadata mutation: `setReadOnly()`.
- Size mutation: `resizeFile()`, `truncateFile()`, and `File::resize()`.

See @ref filesystem_metadata.

## Directory and path mutation families

- Creation: `createDirectory()`, `createDirectories()`.
- Enumeration: bounded-memory `DirectoryCursor` or materializing `listDirectory()`.
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

Every public FileSystem operation is `noexcept`. Allocation and standard-library exceptions arising inside an operation are translated to
`OutOfMemory`, `EncodingFailed`, `InvalidArgument`, or `Unknown` according to the operation. Expected native failures use the portable IO error model
and may include native diagnostic data. Diagnostic message creation is best-effort: if it cannot allocate, the portable and native codes are preserved
with an empty message.

Caller-side construction of `std::filesystem::path`, owning strings, options, or other arguments occurs before function entry and is not contained by
the FileSystem `noexcept` boundary.

Operations may block on storage, sharing, locks, metadata, traversal, or flush work. Different handle objects and free operations may be called
concurrently. The same handle or lock object is not internally synchronized. Current-directory mutation remains process-wide.

## Package and binary boundary

FileSystem is a static library. Its installed package exports `GameWIP::FileSystem`, installs `filesystem/filesystem.h`, and resolves the exact
matching IO and Unicode packages. IO remains the public API dependency; Unicode is used internally for FileSystem-owned text/native trust boundaries.
FileSystem has no generated export header.

Public inheritance from IO interfaces and exposure of standard-library types require compatible compiler, standard-library, runtime, and build
settings. Internal declarations under `filesystem/internal` and platform code are not installed API.
