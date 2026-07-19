@page filesystem_metadata Metadata and predicates

## `EntryInfo`

`getEntryInfo()` returns portable metadata for one existing entry:

- `kind`: `RegularFile`, `Directory`, `Symlink`, or `Other`;
- `sizeBytes`: meaningful only when `hasSize` is true;
- `lastWriteTime`: meaningful only when `hasLastWriteTime` is true;
- `readOnly`: portable basic read-only state.

Portable size is available for regular files. Other kinds can report `hasSize == false` rather than inventing a value.

The read-only field is not a complete permissions, ownership, ACL, or access-control model. `setReadOnly()` modifies only the backend's portable basic read-only representation.

## Predicates and value queries

Missing paths are successful `false` results only for:

- `exists()`;
- `isRegularFile()`;
- `isDirectory()`;
- `isSymlink()`.

Value queries require an existing entry:

- `getEntryInfo()` returns `NotFound` when missing;
- `getFileSize()` returns `NotFound` when missing and `InvalidArgument` when the resolved entry is not a regular file with portable size;
- `getLastWriteTime()` returns `NotFound` when missing;
- `isReadOnly()` returns `NotFound` when missing.

Always inspect `BoolResult::status`; `false` is not meaningful when the status failed.

## Time representation

`Types::FileTime` uses `std::chrono::system_clock::time_point`. Native conversion preserves the closest representable value. Precision can be reduced, and an unrepresentable timestamp returns `SizeLimitExceeded`.

A successful timestamp is a snapshot. It can become stale immediately under concurrent filesystem activity.

## Size mutation

`resizeFile()` resizes one existing regular file; `truncateFile()` is the zero-size convenience operation. Both apply `MutationOptions::symlinkPolicy`.

`File::resize()` performs the same operation through an open writable handle and follows the position rule described by @ref filesystem_file_open_modes.

Growing a file does not promise a particular physical allocation policy beyond the platform's normal file semantics.

## Metadata copy

`CopyMetadataMode::Basic` requests last-write time and read-only state after content copying. Metadata failure is returned even when destination content was copied completely. See @ref filesystem_directory_operations.

## Related pages

- @ref filesystem_symlink_policies
- @ref filesystem_directory_operations
- @ref filesystem_path_operations
