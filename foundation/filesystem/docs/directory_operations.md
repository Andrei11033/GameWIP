@page filesystem_directory_operations FileSystem directories, copy, move, and removal

## Directory creation

`createDirectory()` creates one level. `createDirectories()` creates missing parents.

`CreateDirectoryOptions::succeedIfAlreadyExists` treats an existing directory as success. An existing non-directory remains a conflict. Existing path components are resolved according to the selected symlink policy.

## Direct-child listing

`listDirectory()` returns direct children in backend/native order. The result is not sorted and is not a filesystem snapshot; concurrent changes can affect enumeration and metadata.

Filters for entry kind and hidden state are applied before `maxEntries`. Filtered-out entries do not consume the returned-entry limit.

When another matching child exists beyond the accepted limit, the operation returns `SizeLimitExceeded` and preserves collected entries. `kNoEntryLimit` removes only the caller limit.

`DirectoryEntry::path` is the supplied parent path joined with the child name. A relative parent produces relative child paths.

`ListDirectoryResult` owns every accepted entry, so peak result storage is proportional to the number and path length of returned children. `DirectoryCursor` provides the same filters, ordering, symlink policy, and entry limit without retaining siblings. On Win32, child metadata is queried relative to the retained directory handle, avoiding a complete ancestry traversal for every entry.

```cpp
GameWIP::FileSystem::DirectoryCursor cursor;
if (auto status = cursor.open("assets"); !status.ok())
{
    return 1;
}
for (;;)
{
    auto next = cursor.next();
    if (!next.status.ok())
    {
        return 1;
    }
    if (!next.hasEntry)
    {
        break;
    }
    // Process next.entry without retaining every sibling.
}
```

The API intentionally exposes no public recursive iterator. Recursive traversal belongs in caller code unless the requested operation is tree removal.

## Copying one file

`copyFile()` copies one regular file. It does not copy directories recursively or preserve symbolic-link identity.

- Equivalent source and destination paths return `InvalidArgument`.
- `ReplaceMode` controls destination conflict behavior.
- `createParentDirectories` optionally creates missing destination parents.
- `CopyMetadataMode::Basic` copies portable last-write time and read-only state after contents.
- `flushMode` requests destination durability before close.

Copying is not atomic and is not a source snapshot. Concurrent source changes can produce `CopyFailed`. Once the destination is created or truncated, a later read, write, flush, close, size-consistency, or metadata failure can leave partial or complete destination content.

The implementation compares copied byte progress with source metadata captured for the operation; a mismatch can report `CopyFailed` rather than silently accepting a changing source.

## Moving or renaming

`movePath()` performs one native rename or move.

- Equivalent source and destination paths succeed without mutation.
- `ReplaceMode` controls destination conflict behavior.
- `createParentDirectories` optionally creates missing destination parents.
- Cross-volume moves return `MoveFailed`.
- There is no copy-and-delete fallback.

Native rename success is the move's linearization point. Concurrent recreation of the source or removal/replacement of the destination after that point does not change the returned success.

## Removing entries

`RemoveOptions::succeedIfMissing` converts a missing target to success.

`removeFile()` removes one regular-file or accepted link-like entry. `removeEmptyDirectory()` removes one empty directory and reports `DirectoryNotEmpty` when that condition is distinguishable.

Open handles can prevent rename, replacement, or removal unless their sharing policy includes `FileShare::Delete`.

## Tree removal

`removeDirectoryTree()` uses an explicit traversal stack whose retained state is proportional to depth rather than total entry count.

- The selected symlink policy applies to the initial path only.
- Discovered symlinked directories are never followed.
- `maxEntries` counts successfully removed entries, including the root.
- A limit exactly equal to the tree's removed-entry count can succeed.
- A zero limit performs no removal and returns `SizeLimitExceeded` for an existing target.
- A failure preserves `removedEntries`; removal is incremental, not transactional.

## Related pages

- @ref filesystem_symlink_policies
- @ref filesystem_metadata
- @ref filesystem_atomic_write
- @ref filesystem_troubleshooting
