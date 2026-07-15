@page filesystem_symlink_policies FileSystem symlink policies

`SymlinkPolicy` controls how policy-bearing operations resolve the supplied path. The default is `DoNotFollow` because a path check followed by an ordinary reopen can be replaced between those steps.

## Policies

| Policy | Intermediate components | Final component |
| --- | --- | --- |
| `DoNotFollow` | Reject symlinks | Inspect or operate on the link-like entry itself where the operation accepts it |
| `FollowFinal` | Reject symlinks | Resolve the final link target |
| `FollowAll` | Use ordinary filesystem resolution | Use ordinary filesystem resolution |

Strict policies must be enforced during handle-relative/native traversal rather than by checking a path and reopening it later. When the backend cannot preserve the requested contract, the operation returns `Unsupported`.

## Operation-family behavior

### Queries and metadata

Queries can inspect the link itself or its resolved target according to policy. `isSymlink()` with `DoNotFollow` is the direct way to test the final link-like entry.

### File opening and whole-file I/O

Open operations apply the selected policy while resolving the file. Whole-file helpers inherit the policy from their nested or operation-specific options.

### Directory creation and listing

Creation applies the policy to existing path components. Listing applies its policy when opening the directory and obtaining child metadata.

The listed child path remains the supplied directory path joined with the child name. The metadata can describe the child link or the resolved target depending on policy.

### Copy

`copyFile()` applies the selected policy while resolving the source and destination path. It copies one regular file, not a symbolic link object and not a directory tree.

### Move and removal

`movePath()` applies the selected policy to source resolution and destination parent traversal. Rename/removal-style mutation of a final symlink target is not always implementable without changing the safety contract. `FollowFinal` or `FollowAll` can therefore return `Unsupported` when the final supplied path is a symlink.

With `DoNotFollow`, operations act on the link-like entry itself where that operation accepts the entry kind.

### Tree removal

`removeDirectoryTree()` applies the selected policy only to the initial path. During traversal it never follows symlinked directories discovered below the root; those link entries are removed rather than traversed.

### Atomic replacement

Atomic write resolves the destination according to policy before creating the same-directory temporary file. Following a destination that is itself a symlink may be unsupported when safe replacement of the resolved target cannot be guaranteed.

## Canonicalization is different

`canonicalPath()` and `weaklyCanonicalPath()` use ordinary standard-filesystem resolution and do not accept `SymlinkPolicy`. They produce path values; they are not substitutes for a race-resistant policy-bearing operation.

## Security guidance

Choose `FollowFinal` or `FollowAll` only when link traversal is part of the caller's intended trust model. Do not perform a separate query and assume that a later open or mutation sees the same entry.

On Win32, strict `movePath()` and atomic-write commits issue the final rename relative to the retained, validated destination-parent handle. Concurrent replacement of an ancestor spelling cannot redirect that native commit. This protection does not turn general path queries into reservations; later operations must still enforce their own symlink policy.

## Related pages

- @ref filesystem_path_operations
- @ref filesystem_metadata
- @ref filesystem_directory_operations
- @ref filesystem_atomic_write
