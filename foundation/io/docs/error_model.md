@page io_error_model IO error model

`GameWIP::IO::Types::Status` is the shared error-reporting shape for expected I/O failures.

## ErrorCode

```cpp
namespace GameWIP::IO::Types {

enum class ErrorCode {
    Success,

    InvalidArgument,
    Unsupported,
    NotOpen,
    AlreadyOpen,

    NotFound,
    AlreadyExists,
    PermissionDenied,
    PathTooLong,

    IsDirectory,
    NotDirectory,
    NotSeekable,
    EndOfStream,

    OpenFailed,
    ReadFailed,
    WriteFailed,
    FlushFailed,
    CloseFailed,
    SeekFailed,
    StatFailed,
    RemoveFailed,
    ReplaceFailed,
    CopyFailed,
    MoveFailed,
    ResizeFailed,
    LockFailed,
    UnlockFailed,
    DirectoryCreateFailed,
    DirectoryListFailed,
    DirectoryNotEmpty,

    PartialRead,
    PartialWrite,
    SizeLimitExceeded,
    OutOfMemory,

    ResourceBusy,
    StorageFull,
    BrokenPipe,
    Interrupted,

    EncodingFailed,
    NativeFailure,
    Unknown
};

struct Status {
    ErrorCode code = ErrorCode::Success;
    std::int64_t nativeCode = 0;
    std::string message;

    [[nodiscard]] bool ok() const noexcept;
};

} // namespace GameWIP::IO::Types
```

## Stable error names

`GameWIP::IO::errorCodeName()` returns a stable string literal for an `ErrorCode` value.

Use this for diagnostics, tests, and logs when a stable symbolic name is more useful than `Status::message`.

`Status::message` is developer-facing diagnostic text and is not a stable machine-readable field.

## Status helpers

`GameWIP::IO::makeStatus()` creates a `Status` from a portable code, optional native code, and optional diagnostic message.

`GameWIP::IO::successStatus()` creates a default successful status.

These helpers are public so resource-owning libraries such as FileSystem and Terminal can create statuses that use the shared IO shape consistently.

Constructing a non-empty diagnostic message may allocate. Code-only statuses avoid that cost in allocation-sensitive failure paths.

## Status contract

`Status::ok()` returns true only for `ErrorCode::Success`.

`nativeCode` carries backend-native error data when a concrete backend has such data. IO itself has no platform backend in v1, but FileSystem and Terminal may return native error details through the shared status shape.

Expected I/O failures return `Status` rather than throwing.

Allocation failure inside status-returning helpers is converted to `OutOfMemory` where practical. Construction and direct container operations may still throw allocation exceptions when they do not return `Status`.

## End-of-stream

End-of-stream can be reported through `ReadResult::endOfStream`.

`ErrorCode::EndOfStream` exists for APIs where returning an error-like code is the clearest contract, but ordinary completed reads should not require callers to treat end-of-stream as a hard failure.

## Partial transfers

Partial reads and writes must be observable. A function that accepts or produces fewer bytes than requested should report both the byte count and the status that explains whether the operation can continue.

Whole-stream helpers return `PartialRead` when a reader reports a known remaining size but reaches end-of-stream before that many bytes are produced.

Whole-stream helpers return `ReadFailed` or `WriteFailed` when a backend reports an impossible byte count or cannot make progress without reporting end-of-stream or a failure.

## Size limits

`SizeLimitExceeded` means the requested or observed stream size cannot be accepted.

For known-size reads, the helper returns it before reading when the remaining size exceeds `maxBytes`. For unknown-size reads, the helper returns it after observing a byte beyond the limit. Bytes collected up to the limit remain available in the result.

`OutOfMemory` means a required allocation failed. It is distinct from a caller limit or a container maximum being exceeded.

## Resource and stream failures

`ResourceBusy` is intended for lock/share/resource conflicts, such as a future FileSystem backend failing to open a file because another process has incompatible sharing rules.

`StorageFull` is intended for disk-full or quota-full write failures.

`BrokenPipe` is intended for pipe/redirected-stream failures, such as a future Terminal backend writing to a closed redirected stdout/stderr pipe.

`Interrupted` is intended for platform operations interrupted before completion.
