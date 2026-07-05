@page filesystem_directory_operations FileSystem directory operations

`listDirectory()` returns direct children in backend/native order. The API intentionally exposes no public iterator or recursive listing operation.

`ListDirectoryOptions` controls included entry kinds, hidden entries, symlink metadata behavior, and `maxEntries`.

`CreateDirectoryOptions::symlinkPolicy` controls traversal through existing path components. The default is `DoNotFollow`; callers must opt into `FollowFinal` or `FollowAll` when symlink traversal is intentional.

When `maxEntries` is reached before completion, the operation returns `SizeLimitExceeded` and preserves entries already collected. There is no separate truncation-success state.

`removeDirectoryTree()` is the primitive tree-removal operation. It:

- retains traversal state proportional to directory depth rather than the total entry count;
- never follows symlinked directories discovered during traversal;
- obeys the requested policy for the initial path;
- returns `SizeLimitExceeded` when `maxEntries` stops removal;
- reports how many entries were removed before success or failure;
- performs no removal when `maxEntries` is zero.

Removal is intentionally incremental, not transactional. If a limit or backend failure is reached, entries already reported in `removedEntries` remain removed.

Final symlink following for native rename/removal-style operations returns `Unsupported` when the backend cannot perform the operation on the resolved target without changing the requested safety contract.

`removeEmptyDirectory()` reports `DirectoryNotEmpty` for a non-empty directory where the backend can identify that condition.

`copyFile()` copies one regular file only. `CopyMetadataMode::Basic` additionally requests portable last-write-time and read-only metadata. Copying is not an atomic replacement; failure can leave a newly created or truncated partial destination. Recursive directory copy belongs above FileSystem.

`movePath()` performs a native move or rename. Cross-volume moves return `MoveFailed`; FileSystem does not silently copy and delete.
