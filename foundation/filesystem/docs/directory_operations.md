@page foundation_filesystem_directory_operations FileSystem directory operations

`listDirectory()` returns direct children in backend/native order. v1 intentionally exposes no public iterator or recursive listing API.

`ListDirectoryOptions` controls included entry kinds, hidden entries, symlink metadata behavior, and `maxEntries`.

`CreateDirectoryOptions::symlinkPolicy` controls traversal through existing path components. The default follows ordinary filesystem resolution; security-sensitive callers can request a stricter policy.

When `maxEntries` is reached before completion, the operation returns `SizeLimitExceeded` and preserves entries already collected. There is no separate truncation-success state.

`removeDirectoryTree()` is the primitive recursive removal operation. It:

- never follows symlinked directories discovered during traversal;
- obeys the requested policy for the initial path;
- returns `SizeLimitExceeded` when `maxEntries` stops removal;
- reports how many entries were removed before success or failure.

`removeEmptyDirectory()` reports `DirectoryNotEmpty` for a non-empty directory where the backend can identify that condition.

`copyFile()` copies one regular file only. `CopyMetadataMode::Basic` additionally requests portable last-write-time and read-only metadata. Recursive directory copy belongs above FileSystem.

`movePath()` performs a native move or rename. Cross-volume moves return `MoveFailed`; FileSystem does not silently copy and delete.
