@page io_error_model IO error model

`GameWIP::IO::Types::Status` is the shared error-reporting shape for expected I/O failures.

## Status fields

| Field | Meaning |
| --- | --- |
| `code` | Portable category used for program decisions. |
| `nativeCode` | Backend-native error value when one exists, otherwise zero. |
| `message` | Optional developer-facing diagnostic text. It is not stable for parsing. |

`Status::ok()` returns true only when `code == ErrorCode::Success`.

A successful status should normally use `nativeCode == 0` and an empty message. Backends may attach diagnostic detail to failures when the additional allocation and text are useful.

## Error-code categories

The generated API reference documents each enumerator. Use the following categories when selecting a code.

### Contract and state

| Code | Use when |
| --- | --- |
| `InvalidArgument` | Input violates the API contract, including an unknown option enumerator or an internally inconsistent capability result. |
| `Unsupported` | The operation or capability is intentionally unavailable and no more specific capability code applies. |
| `NotOpen` | An operation requires an open resource or memory object. |
| `AlreadyOpen` | Opening was requested for an already-open resource. |

### Resource lookup, path, and kind

| Code | Use when |
| --- | --- |
| `NotFound` | The requested resource does not exist. |
| `AlreadyExists` | Creation or replacement policy rejects an existing resource. |
| `PermissionDenied` | Access policy rejects the operation. |
| `PathTooLong` | A backend path limit is exceeded. |
| `IsDirectory` | A directory was supplied where a non-directory resource was required. |
| `NotDirectory` | A non-directory was supplied where a directory was required. |
| `NotSeekable` | Position, size, or seek behavior is unavailable for a stream. |
| `EndOfStream` | An API explicitly represents end-of-stream as a status. Ordinary reads should prefer `ReadResult::endOfStream`. |

### Generic operation failures

Use `OpenFailed`, `ReadFailed`, `WriteFailed`, `FlushFailed`, `CloseFailed`, `SeekFailed`, `StatFailed`, `RemoveFailed`, `ReplaceFailed`, `CopyFailed`, `MoveFailed`, `ResizeFailed`, `LockFailed`, `UnlockFailed`, `DirectoryCreateFailed`, or `DirectoryListFailed` when the named operation failed and no more specific portable category applies.

Use `DirectoryNotEmpty` when removal fails specifically because a directory still contains entries.

### Transfer, size, and allocation

| Code | Use when |
| --- | --- |
| `PartialRead` | A reader promised a known remaining byte count but reached end-of-stream before producing it. |
| `PartialWrite` | A backend chooses to classify an incomplete write explicitly. The generic write-all helper does not synthesize this code for every successful short write. |
| `SizeLimitExceeded` | A caller limit, destination representation, or supported stream-size range is exceeded. |
| `OutOfMemory` | A required allocation failed in an operation whose status contract converts allocation failure. |

### Resource and platform state

| Code | Use when |
| --- | --- |
| `ResourceBusy` | Sharing, lock, or active-use state prevents the operation. |
| `StorageFull` | Storage or quota capacity is exhausted. |
| `BrokenPipe` | A pipe or redirected output no longer has a reader. |
| `Interrupted` | The operation was interrupted before completion. |

### Encoding and fallback

| Code | Use when |
| --- | --- |
| `EncodingFailed` | Text encoding or conversion failed. IO text helpers themselves do not validate UTF-8. |
| `NativeFailure` | A backend-native failure has no useful portable category. |
| `Unknown` | The failure category cannot be determined. Prefer a more specific code whenever possible. |

## Selecting the primary code

Return the most specific portable category that describes the failure. Use generic operation codes only when no better category is available. For example, a file open denied by access control should normally use `PermissionDenied`, not `OpenFailed`.

The native code and message supplement the portable category; they must not replace it.

## Symbolic and numeric stability

`errorCodeName()` returns stable symbolic names for diagnostics, logs, and tests. Unknown enumerator values map to `"Unknown"`.

`ErrorCode` numeric values are not serialization identifiers or protocol values. Do not persist enum ordinals or expose them as a cross-version wire contract. Persist an application-owned representation when storage or protocol stability is required.

## Partial progress

Transfer count and status are independent:

- A reader or writer may report nonzero progress with a failure status.
- Whole-stream helpers preserve valid progress produced by the final failing call.
- Callers decide whether partial output is useful, retryable, or must be discarded.
- A backend must never report a byte count larger than the supplied span.

A successful short write is not itself a failure. `writeAllBytes()` retries it. A successful zero-byte write while input remains is invalid progress and becomes `WriteFailed`.

For known-size reads, early end-of-stream becomes `PartialRead`. For unknown-size reads, a successful zero-byte read without end-of-stream becomes `ReadFailed` because the helper cannot make progress.

## End-of-stream

`ReadResult::endOfStream` is independent of `bytesRead`:

- The final non-empty read may return bytes and set `endOfStream = true`.
- An empty read at the end may return success, zero bytes, and `endOfStream = true`.
- A zero-byte success with `endOfStream = false` does not permit a whole-stream helper to continue safely.

## Status helpers and allocation

`makeStatus()` accepts a portable code, optional native code, and optional owning message. The function is `noexcept`; however, construction of the message argument happens before function entry and may allocate.

`successStatus()` creates a default successful status. Code-only statuses avoid diagnostic-string allocation.

Whole-stream and memory-writer operations convert the allocations they perform internally to `OutOfMemory` where documented. Constructors, `MemoryWriter::reserve()`, `MemoryWriter::text()`, and exceptions thrown by custom backend implementations are not universally converted because those operations do not all have a status-returning allocation boundary.

## Related pages

- @ref io_reader_writer_contract
- @ref io_runtime_performance
- @ref io_troubleshooting
