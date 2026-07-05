@page filesystem_public_api FileSystem public API

Include `filesystem/filesystem.h`. Installed-package consumers link `GameWIP::FileSystem`; builds within the source tree link the short `FileSystem` target. See @ref filesystem_quick_start for complete CMake examples.

Passive values, options, and results live in `GameWIP::FileSystem::Types`. Active resource owners and free operations live directly in `GameWIP::FileSystem`.

## Paths and text

`Types::Path` is the project spelling for `std::filesystem::path`. Store and pass paths through that type, but use `pathFromUtf8()` and `pathToUtf8()` at text boundaries where UTF-8 behavior must be explicit.

`readAllText()`, `writeAllText()`, `appendText()`, and `writeAllTextAtomic()` treat text as UTF-8 bytes without encoding conversion.

## Handles

`FileReader` is a read-only `IO::Reader`. `FileWriter` is a write-only `IO::Writer`. `File` is a read/write object for modification workflows.

All handle types start closed, are move-constructible, are not copyable, and do not support move assignment. A failed `open()` leaves the object closed. Repeated `close()` calls succeed unless an active lock keeps the handle busy.

Use:

- `FileReaderOpenOptions` for read sharing and symlink behavior.
- `FileWriterOpenOptions` for write mode, sharing, symlink behavior, parent creation, and close flushing.
- `FileOpenOptions` for read/write access, open mode, initial position, sharing, symlink behavior, parent creation, and close flushing.

See @ref filesystem_file_open_modes for mode, append, sharing, and lock details.

## Whole-file helpers

`readAllBytes()` and `readAllText()` open, drain, and close a file. `ReadFileOptions` controls the open options, maximum accepted bytes, and transfer buffer size.

`writeAllBytes()` and `writeAllText()` replace exact contents through a normal writer and return `IO::Types::WriteResult` so payload progress is preserved if a later flush or close fails.

`appendBytes()` and `appendText()` use append-mode writer handles and also return `WriteResult`.

`writeAllBytesAtomic()` and `writeAllTextAtomic()` replace through a same-directory temporary file and return only `IO::Types::Status` because the externally visible contract is complete path replacement.

## Metadata and predicates

`exists()`, `isRegularFile()`, `isDirectory()`, `isSymlink()`, and `isReadOnly()` return `Types::BoolResult`. Missing paths are successful `false` results for predicates.

`getEntryInfo()`, `getFileSize()`, and `getLastWriteTime()` return value results and report `NotFound` when the target is missing.

`setReadOnly()`, `resizeFile()`, and `truncateFile()` mutate one existing entry and use the requested symlink policy.

## Directories and tree operations

`createDirectory()` creates one level. `createDirectories()` creates missing parents. `listDirectory()` returns direct children in native order and can filter kinds, hidden entries, and collected count.

`removeFile()` removes one file or link-like file entry. `removeEmptyDirectory()` removes one empty directory. `removeDirectoryTree()` removes a tree with an explicit traversal stack and never follows discovered symlinked directories.

See @ref filesystem_directory_operations for the detailed directory contract.

## Copy and move

`copyFile()` copies one regular file. It can optionally copy portable basic metadata and flush the destination. The operation is not atomic: after destination creation or truncation, a later read, write, flush, close, or metadata failure may leave a partial destination.

`movePath()` performs one native rename or move. Cross-volume moves return `MoveFailed`; FileSystem does not silently copy and delete.

## Path helpers

`getCurrentDirectory()` and `setCurrentDirectory()` access the process current directory. Current-directory changes are process-global.

`parentPath()`, `filename()`, `stem()`, `extension()`, `replaceExtension()`, and `joinPath()` wrap common path transformations with status-returning failure behavior.

`isAbsolutePath()`, `isRelativePath()`, `absolutePath()`, `canonicalPath()`, `weaklyCanonicalPath()`, and `getTemporaryDirectoryPath()` expose common path queries and resolution.

## Failure, blocking, and threading

Expected failures use `IO::Types::Status`; public operations are `noexcept`.

Path operations may block on storage, sharing, locks, metadata, directory traversal, or operating-system flush work. Different handle objects may be used concurrently, but a single handle or lock object is not internally synchronized.

Free path operations are independently callable from multiple threads. `setCurrentDirectory()` affects the whole process.
