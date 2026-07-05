@page filesystem_atomic_write FileSystem atomic writes

`writeAllBytesAtomic()` and `writeAllTextAtomic()` replace exact file contents through a temporary file in the destination directory.

The operation:

1. validates `temporaryNamePrefix` as a filename prefix rather than a path;
2. resolves the destination according to `symlinkPolicy`;
3. creates a unique temporary file in the destination directory with restrictive access;
4. writes the complete content;
5. flushes it according to `flushMode`;
6. commits it with one native rename or replacement according to `replaceMode`;
7. optionally flushes the parent directory;
8. removes the temporary file after failure where practical.

There is no non-atomic fallback. Before the commit point, failure leaves an existing destination unchanged. Concurrent path observers see either the old destination content or the complete new content, never an in-place partial rewrite.

## Metadata and access

Atomic write guarantees content replacement, not identity preservation. An open handle or hard link to the previous file object may continue to observe that old object after the path is replaced.

Temporary files are created with restrictive access. The backend must not silently broaden access because copying or merging security-relevant metadata failed. If it cannot perform replacement without a known access broadening, it returns a failure before commit.

Successful replacement does not guarantee preservation of timestamps, ownership, complete ACLs, extended attributes, named streams, compression, encryption, file identifiers, or hard-link identity. A backend may preserve native metadata where its atomic replacement primitive does so, but callers needing a specific metadata contract must manage that separately.

`flushParentDirectory` requests directory-entry durability after replacement. When it is enabled, a backend that cannot flush the parent directory reports failure instead of silently downgrading the durability contract.

`temporaryNamePrefix` is an owning `std::string`, so an `AtomicWriteOptions` value may be stored or moved without depending on a caller-owned character buffer. The prefix must be non-empty: it cannot contain path separators, name `.` or `..`, or contain an embedded NUL. Invalid prefixes return `InvalidArgument` before a temporary file is created.

Atomic write does not promise multi-file transactions or database durability. Text is treated as UTF-8 bytes without BOM handling, validation, or encoding conversion.
