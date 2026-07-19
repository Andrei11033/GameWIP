@page filesystem_whole_file_io Whole-file I/O

Whole-file helpers are for one complete operation whose open, transfer, optional flush, and close sequence belongs to FileSystem. Use explicit handles for incremental protocols, seeking, locks, or caller-controlled lifetime.

## Reading

`readAllBytes()` and `readAllText()` open a `FileReader`, drain it through the IO whole-stream helper, and close it.

`ReadFileOptions` controls:

- the underlying `FileReaderOpenOptions`;
- `maxBytes`, a hard retained-data limit;
- `bufferSize`, the transfer-buffer size, which must be greater than zero.

`IO::kNoByteLimit` removes only the caller-imposed limit. Allocation, container, address-space, and backend limits still apply.

Reaching `maxBytes` is not successful truncation. A normal `FileReader` supplies size and position, so IO rejects a known remaining size above the limit before reading and accepts a size exactly equal to the limit. If size or position is unavailable, IO instead uses its one-byte probe; extra data then produces `SizeLimitExceeded` without retaining the probe byte. Size metadata is a snapshot, so concurrent file mutation remains subject to the documented filesystem race limitations.

A failure can return collected bytes or text. This includes failures after partial reads and close failures after all data was collected. Inspect the status and payload together.

## Exact-content writes

`writeAllBytes()` and `writeAllText()` open a normal writer according to `WriteFileOptions`, write the complete payload through `IO::writeAllBytes()`, optionally flush, and close.

Modes are deliberately limited to exact-content behavior:

- `CreateNew` fails if the destination exists;
- `CreateOrTruncate` creates or truncates;
- `TruncateExisting` requires an existing file and truncates it.

An empty payload still performs the requested open/create/truncate, flush, and close sequence. In particular, `CreateOrTruncate` can replace existing content with an empty file.

These operations are not atomic. A create or truncate can occur before a later write, flush, or close failure. `bytesWritten` reports payload accepted before the final status; it may equal the full payload when a later flush or close fails.

## Append

`appendBytes()` and `appendText()` use true append-mode handles:

- `AppendOrCreate` creates a missing file;
- `AppendExisting` reports `NotFound` for a missing file;
- each write targets the then-current end of file;
- the append handle is not seekable.

The helpers preserve accepted payload progress exactly like non-atomic exact-content writes.

## Text and byte overloads

Text helpers reinterpret `std::string_view` as bytes. They do not validate encoding, add or remove a BOM, or translate line endings.

Vector byte overloads forward to the span overloads. FileSystem does not retain caller spans, string views, or vector storage after a call returns.

## Partial progress and terminal failures

Direct handle writes and whole-file helpers can return a nonzero count with failure. A successful direct `write()` may also accept fewer bytes than requested; callers using handles must inspect `bytesWritten` and loop or use IO's whole-stream helper when complete transfer is required.

A close failure leaves the handle open and can be retried. Whole-file helpers cannot return their internal handle, so they preserve the close status and any useful payload progress in the returned result.

## Atomic replacement

Use `writeAllBytesAtomic()` or `writeAllTextAtomic()` when readers must observe either the previous complete file or the new complete file at the destination path. See @ref filesystem_atomic_write; atomic helpers do not expose temporary-file byte progress.

## Related pages

- @ref filesystem_file_open_modes
- @ref filesystem_atomic_write
- @ref filesystem_metadata
- @ref io_reader_writer_contract
