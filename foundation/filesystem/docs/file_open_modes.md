@page filesystem_file_open_modes FileSystem open modes

## Handle lifecycle

`FileReader`, `FileWriter`, and `File` start closed and expose member `open()` operations.

- opening an open object returns `AlreadyOpen`;
- a failed open leaves the object closed;
- operations requiring a handle return `NotOpen` while closed;
- `close()` is idempotent;
- a closed object may be reopened;
- destruction closes best-effort without throwing;
- move construction transfers the complete native state;
- move assignment is deleted because replacing an active destination could hide close, flush, or unlock failure.

Call `close()` explicitly when close or flush failure must be observed.

## Handle roles

`FileReaderOpenOptions` controls read-only sharing and symlink resolution.

`FileWriterOpenOptions` controls write mode, sharing, symlink resolution, optional parent creation, and flush-on-close behavior.

`FileOpenOptions` controls read/write access, open mode, initial position, sharing, symlink resolution, optional parent creation, and flush-on-close behavior.

`FileInitialPosition::End` performs one initial seek. It is not append mode.

Modes that may create or truncate a file require `FileAccess::Write` or `ReadWrite`. A non-`None` `flushOnClose` also requires write access. Invalid option combinations and unknown enum values return `InvalidArgument` before opening a handle.

Open options default to `SymlinkPolicy::DoNotFollow`. Callers must opt into `FollowFinal` or `FollowAll` when opening through a symlink is intentional.

## Append

`FileWriterMode::AppendOrCreate` and `AppendExisting` are true append modes:

- the handle is non-seekable;
- each write targets the then-current end of file atomically with respect to other append handles where the backend supports it;
- `position()` may be unsupported because another writer can change the endpoint.

Whole-file exact-content helpers use `WriteFileOptions` and `WriteFileMode`. Append helpers use `AppendFileOptions` and `AppendMode`. These operation-specific types prevent callers from supplying a valid general writer mode that is invalid for the selected helper.

Non-atomic whole-file and append helpers return `IO::Types::WriteResult`. `bytesWritten` is payload progress, including bytes accepted by a final failing write. A later flush or close failure returns that failure while preserving the complete payload byte count already accepted.

## Sharing

`FileShare` is a bitmask open-time policy and is separate from `FileLockMode`.

- `Read`: allows other opens requesting read access;
- `Write`: allows other opens requesting write access;
- `Delete`: allows rename, removal, or replacement while the handle remains open;
- `ReadWrite`: convenience combination of `Read | Write`;
- `All`: convenience combination of `Read | Write | Delete`;
- `None`: allows none of those operations.

Options default to `All`, the naturally portable and least surprising policy for interoperating with external tools and atomic replacement. Use locks for portable coordination. A backend that cannot enforce an explicitly restrictive policy returns `Unsupported`; it must not silently weaken the policy.

## Locking

Locks are acquired from an open object:

```cpp
auto shared = reader.tryLockShared();
auto exclusive = writer.tryLockExclusive();
```

Lock acquisition is non-blocking and covers the complete file. `LockOutcome::WouldBlock` is a successful expected outcome with an inactive lock. Backend failures use `LockFailed`.

Locks are process-visible coordination primitives but may be advisory on platforms whose native locks do not prevent uncooperative I/O. A backend that cannot provide compatible shared/exclusive whole-file locking returns `Unsupported`.

`FileLock` owns unlock responsibility. Failed explicit unlock remains active and may be retried. A file handle cannot close while locks acquired from it remain active; explicit close returns `ResourceBusy`.
