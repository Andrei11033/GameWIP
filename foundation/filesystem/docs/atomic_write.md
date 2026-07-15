@page filesystem_atomic_write FileSystem atomic writes

`writeAllBytesAtomic()` and `writeAllTextAtomic()` replace exact contents through a temporary file in the destination directory.

## Operation sequence

The operation:

1. validates `temporaryNamePrefix` as a filename prefix, not a path;
2. resolves the destination according to `symlinkPolicy`;
3. optionally creates missing parent directories;
4. creates a unique same-directory temporary file with restrictive access;
5. writes the complete payload, including a valid empty payload;
6. flushes the temporary file according to `flushMode`;
7. commits using one native rename or replacement according to `replaceMode`;
8. optionally flushes the parent directory;
9. attempts best-effort temporary cleanup after failure.

There is no non-atomic fallback. Before commit, failure leaves an existing destination unchanged. At the destination path, concurrent observers see either the previous file or the complete replacement rather than an in-place partial rewrite.

## Replacement and races

`FailIfExists` checks destination policy before commit, but concurrent filesystem activity can still determine the final native result. `ReplaceExisting` permits replacement where the backend can satisfy the selected sharing, access, and symlink contract.

On the current Win32 backend, strict destination-parent validation and the final absolute-path rename are separate steps. Concurrent destination-ancestor replacement can redirect the commit. Concurrent recreation of the temporary source or removal/replacement of the destination immediately after native rename can also make the operation report failure after the replacement is already visible. Treat an ambiguous failure as potentially post-commit; do not retry blindly over valuable data.

Atomicity applies to replacement of one path. It does not create a transaction across multiple files, directories, or metadata operations.

## Temporary files

`temporaryNamePrefix` is an owning `std::string`. It must be non-empty, contain no path separator or embedded NUL, and not be the complete name `.` or `..`.

Temporary-name collision retries are bounded. Exhaustion returns the final creation failure. Cleanup after failure is best effort, so an orphan temporary file can remain after unusual cleanup or process failures.

## Metadata, identity, and access

Atomic replacement changes the object named by the path. Existing handles and hard links to the previous object can continue to observe that object.

Temporary files use restrictive access. The backend must not silently broaden security-relevant access because metadata preservation failed.

Successful replacement does not promise preservation of timestamps, ownership, complete ACLs, extended attributes, named streams, compression, encryption, file identifiers, or hard-link identity. Callers requiring a specific metadata contract must apply and verify it separately.

## Durability

A successful file flush and rename do not by themselves promise directory-entry durability.

When `flushParentDirectory` is true, inability to flush the parent directory is returned rather than silently weakening the request. That failure occurs after commit, so the replacement may already be visible and must not be retried as though the old destination were necessarily intact.

Atomic write is not a database durability or crash-consistency protocol beyond the explicitly requested file and parent-directory flushes.

## Symlinks

Destination resolution follows `symlinkPolicy`. Following a destination that is itself a symlink may return `Unsupported` when replacement of the resolved target cannot be implemented without weakening the selected race-safety contract.

## Text

Text is treated as bytes. No BOM handling, validation, encoding conversion, or line-ending translation occurs.

## Related pages

- @ref filesystem_whole_file_io
- @ref filesystem_symlink_policies
- @ref filesystem_metadata
- @ref filesystem_troubleshooting
