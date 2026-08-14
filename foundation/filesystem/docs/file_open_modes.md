@page filesystem_file_open_modes Handles, sharing, and locks

## Lifecycle and ownership

`FileReader`, `FileWriter`, and `File` start closed.

- `open()` on an open object returns `AlreadyOpen`.
- A failed open leaves the object closed.
- Operations requiring a handle return `NotOpen` while closed.
- `close()` is idempotent.
- A closed object can be reopened.
- Move construction transfers the complete state and leaves the source closed.
- Copy and move assignment are disabled so implicit replacement cannot hide close, flush, or unlock failure.
- Destructors perform best-effort cleanup and cannot report failure.

Call `close()` explicitly when the result matters. If close or configured `flushOnClose` fails, the object remains open and close can be retried.

The same handle object is not internally synchronized. Spans passed to `read()` and `write()` are call-scoped and are never retained.

## Handle roles

`FileReader` is read-only. `FileWriter` is write-only. `File` selects `Read`, `Write`, or `ReadWrite` access through `Types::File::OpenOptions`.

Modes that create or truncate require write access. A non-`None` `flushOnClose` also requires write access. Invalid combinations return `InvalidArgument` before a native handle is opened.

A `File` opened without the required access returns `PermissionDenied` from the disallowed read or write operation. `File::access()` is meaningful only while open.

## Transfer behavior

Reads and writes can complete partially. Inspect `bytesRead`, `endOfStream`, and `bytesWritten` even when the status is successful.

An empty read performs no transfer but reports the current end-of-stream state. An empty write succeeds without transferring bytes.

`flush(IO::Types::FlushMode::None)` validates the handle and mode but requests no physical flush. Stronger flush modes follow the IO contract.

## Position, size, and resize

Normal file handles are seekable. Append writer modes are not.

`Types::File::InitialPosition::End` performs one seek after open; subsequent writes occur at the current position and do not have append semantics.

`File::resize()` requires write access. On success it attempts to restore the previous position when that position still fits. If shrinking places the old position beyond the new end, the position remains at the new end.

Capability queries such as `canSeek()` are advisory state snapshots. The status returned by the requested operation remains authoritative.

## Append modes

`Types::File::WriterMode::AppendOrCreate` and `AppendExisting` are true append modes:

- each write targets the then-current end of file;
- another append handle can change the endpoint between calls;
- `seek()` and `position()` return `NotSeekable`;
- `size()` remains available.

## Sharing

`Types::File::Share` controls which access other opens may request while the handle remains open:

- `Read` permits read opens;
- `Write` permits write opens;
- `Delete` permits rename, removal, or replacement;
- `ReadWrite` and `All` are convenience masks;
- `None` requests exclusive open-time access.

Options default to `All`, which interoperates with external tools and atomic replacement. Restrictive sharing is a native open contract, not a substitute for cooperative locking. A backend that cannot enforce an explicit restrictive policy returns `Unsupported` rather than weakening it.

## Whole-file locks

Lock acquisition is non-blocking and covers the complete file:

- `FileReader` offers shared locking;
- `FileWriter` offers exclusive locking;
- `File` offers both.

A successful status with `Types::Lock::Outcome::WouldBlock` means no lock was acquired. Backend failures use a failed status.

`FileLock` owns enough native state to remain active after the originating handle object is destroyed. This supports detached lock ownership but has an important lifecycle distinction:

- explicit handle `close()` returns `ResourceBusy` while locks acquired from that handle remain active;
- a handle destructor cannot report that condition and performs best-effort native cleanup;
- the independently owned `FileLock` remains responsible for unlocking.

A failed explicit `unlock()` leaves the lock active and can be retried. `mode()` is meaningful only while `isActive()` is true.

Locks are process-visible coordination primitives, but native locks may be advisory with respect to uncooperative tools. Use sharing to constrain opens and locks to coordinate cooperating code.

## Related pages

- @ref filesystem_whole_file_io
- @ref filesystem_symlink_policies
- @ref filesystem_troubleshooting
- @ref io_reader_writer_contract
