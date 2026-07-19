@page filesystem_troubleshooting Troubleshooting

## A policy-bearing operation returns `Unsupported`

The backend could not enforce the selected `SymlinkPolicy` or restrictive sharing request without weakening it. Final-link move, removal, or atomic replacement with follow semantics are common examples. Review @ref filesystem_symlink_policies rather than replacing the operation with a check-then-open sequence.

## A predicate returns success with `false`

Missing paths are successful `false` only for `exists()`, `isRegularFile()`, `isDirectory()`, and `isSymlink()`.

`isReadOnly()` is a value query and returns `NotFound` for a missing path. `getFileSize()` returns `InvalidArgument` for a non-regular entry.

## An operation returns `InvalidArgument`

Common causes include:

- an empty target path for an operation that requires one;
- an unknown enum value or unsupported flag bits;
- a create/truncate mode without write access;
- `flushOnClose` on a read-only `File`;
- `ReadFileOptions::bufferSize == 0`;
- equivalent source and destination supplied to `copyFile()`;
- an invalid atomic temporary prefix.

Lexical path helpers accept empty paths; path-accessing operations generally do not.

## A handle returns `AlreadyOpen`, `NotOpen`, or `PermissionDenied`

`open()` on an active object returns `AlreadyOpen`. Handle operations return `NotOpen` while closed. `File` returns `PermissionDenied` when the selected access does not permit the requested read, write, flush, or resize operation.

A failed open leaves the object closed.

## `close()` returns `ResourceBusy`

A lock acquired from the handle remains active. Unlock it before explicit close.

A detached `FileLock` can outlive the originating handle object. Handle destruction cannot report close failure, so the detached lock remains responsible for release.

## `close()` or `unlock()` fails

The resource remains active and the operation can be retried. Destruction performs best-effort cleanup but cannot report the outcome.

## Append mode cannot seek

Append modes target the then-current end for each write and return `NotSeekable` from `seek()` and `position()`. Use `FileInitialPosition::End` only when one initial end position is sufficient; it does not protect later writes from concurrent endpoint changes.

## A whole-file read returns `SizeLimitExceeded`

`maxBytes` is a hard accepted-data limit, not a truncation request. Increase it or stream through `FileReader`. A file exactly equal to the limit succeeds; additional data triggers the probe and failure.

`listDirectory()` and `removeDirectoryTree()` use the same error for caller entry limits while preserving completed progress.

## A write reports the full payload with failure

The payload may have been fully accepted before a later flush or close failure. Inspect both `status` and `bytesWritten`. The destination may contain complete or partial new content after non-atomic operations.

## Atomic write fails after replacement

A requested parent-directory flush occurs after commit. Its failure can be returned when the replacement is already visible. Do not assume retrying is equivalent to retrying a pre-commit failure.

## Copy leaves destination content after failure

`copyFile()` is not atomic. It can create or truncate the destination before read, write, flush, close, consistency, or metadata failure. Use atomic replacement when the caller already has the complete replacement payload.

## UTF-8 path text does not round-trip

Use `pathFromUtf8()` and `pathToUtf8()` at explicit text boundaries. Do not assume that direct narrow-string `std::filesystem::path` construction interprets UTF-8 or that path output uses portable separators.

## Relative paths resolve unexpectedly

Relative resolution depends on the process current directory. Another thread or library can change it through process APIs. Prefer absolute paths and avoid uncoordinated `setCurrentDirectory()`.

## Sharing or locking differs across tools

`FileShare` constrains compatible native opens. `FileLock` coordinates compatible lock users and can be advisory relative to uncooperative software. Choose the mechanism for the actual coordination boundary and do not assume unrelated tools honor advisory locks.

## Related pages

- @ref filesystem_file_open_modes
- @ref filesystem_symlink_policies
- @ref filesystem_atomic_write
- @ref filesystem_unicode_paths
