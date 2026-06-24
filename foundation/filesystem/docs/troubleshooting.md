@page foundation_filesystem_troubleshooting FileSystem troubleshooting

## A normal path operation returns `Unsupported`

The most common cause is a requested `SymlinkPolicy` that the backend cannot enforce without a path-replacement race. Public operations default to `DoNotFollow`; choose `FollowFinal` or `FollowAll` only when symlink traversal is intentional.

Final-symlink move and removal with follow semantics may also report `Unsupported` when the backend cannot safely mutate the resolved target while preserving the requested contract.

## `exists()` returns success with `false`

Predicate queries treat a missing path as a successful negative answer. Value queries such as `getEntryInfo()`, `getFileSize()`, and `getLastWriteTime()` report `NotFound` for a missing target.

## An operation returns `InvalidArgument`

Common causes include:

- an empty target path;
- an unknown enum value;
- a create/truncate mode without write access;
- `flushOnClose` on a read-only handle;
- a zero transfer buffer size;
- an atomic temporary-name prefix that is empty, names `.` or `..`, contains a path separator, or contains an embedded NUL.

## A handle returns `AlreadyOpen` or `NotOpen`

`open()` on an already-open `FileReader`, `FileWriter`, or `File` returns `AlreadyOpen`. Operations that require an open handle return `NotOpen` while closed.

Failed `open()` calls leave the object closed. Repeated `close()` calls succeed unless an active lock acquired from the handle still exists.

## `close()` returns `ResourceBusy`

A whole-file lock acquired from the handle is still active. Unlock it first, then close the handle.

If explicit `unlock()` fails, the lock remains active and may be retried. Destruction performs best-effort cleanup but cannot report failure.

## Append mode cannot seek or report a stable position

Append modes are non-seekable because each write targets the then-current end of file. Another append handle may change the endpoint between calls.

Use `File` with a normal open mode when explicit seeking is required. Use `AppendOrCreate` or `AppendExisting` when true append behavior is required.

## A whole-file read returns `SizeLimitExceeded`

`ReadFileOptions::maxBytes` is a hard retained-data limit. It does not request truncation. Increase the limit or stream with `FileReader` when large files are expected.

`listDirectory()` also returns `SizeLimitExceeded` when `maxEntries` is reached; collected entries remain available in the result.

## A write helper reports fewer bytes than requested

Non-atomic write and append helpers return `IO::Types::WriteResult`. The byte count is payload accepted before the final failure, including bytes accepted by a write that is followed by flush or close failure.

Atomic write helpers return only `Status` because partial temporary-file progress is not part of the visible path-replacement contract.

## Atomic write fails after replacing content

When `flushParentDirectory` is true, a backend that cannot flush the parent directory reports that failure instead of silently downgrading the durability request. The path replacement may already be visible when that late durability failure is returned.

Disable `flushParentDirectory` only when directory-entry durability is not required for the caller's use case.

## UTF-8 filenames are not round-tripping

Constructing `std::filesystem::path` from narrow text happens before FileSystem can inspect it. Use `pathFromUtf8()` for UTF-8 input text and `pathToUtf8()` when text output must be UTF-8.

Text file helpers treat file contents as UTF-8 bytes. They do not validate path text or convert file encodings.

## Sharing or locking behaves differently across tools

`FileShare` controls open-time sharing where the backend can enforce it. `FileLock` is a process-visible coordination primitive but may be advisory on platforms whose native locks do not stop uncooperative I/O.

Use sharing to constrain concurrent opens and locks to coordinate cooperating code. Do not assume unrelated tools honor advisory locks.
