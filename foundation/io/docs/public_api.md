@page io_public_api Public API

Include `io/io.h` for the complete IO surface. Focused headers are:

- `io/stream.h`: stream enums, progress payloads, `Reader`, and `Writer`.
- `io/memory.h`: `MemoryReader` and `MemoryWriter`.
- `io/transfer.h`: whole-stream helpers and limits.

Every fallible operation returns `GameWIP::Status::Types::Result<T>` or `Result<void>`. See @ref status for result, status, and diagnostic rules.
Installed consumers link `GameWIP::IO`; source-tree consumers link `IO`.

## Constants

| API | Purpose |
| --- | --- |
| `kNoByteLimit` | Removes the caller-imposed limit from a whole-stream read. Container and address-space limits still apply. |
| `kDefaultBufferSize` | Temporary buffer size for internally buffered unknown-size reads. |

## Payload types

| Type | Purpose |
| --- | --- |
| `Types::SeekOrigin` | `Begin`, `Current`, or `End` for signed seek offsets. |
| `Types::FlushMode` | `None`, `Data`, or best-effort data and metadata flush. |
| `Types::ReadProgress` | Bytes copied and independent end-of-stream state. |
| `Types::WriteProgress` | Bytes accepted by a write. |

The result value carries payload only. Status is read with `.status()`, success with `.ok()`, and an optional partial payload with `.hasValue()` and
`.value()`.

## Reader and Writer

`Reader` and `Writer` are movable, non-copyable, platform-neutral interfaces. Each requires one operation:

```cpp
virtual GameWIP::Status::Types::Result<Types::ReadProgress>
read(std::span<std::byte> destination) noexcept = 0;

virtual GameWIP::Status::Types::Result<Types::WriteProgress>
write(std::span<const std::byte> bytes) noexcept = 0;
```

The base classes provide defaults for lifecycle and optional stream capabilities. Unsupported position, size, and seek operations return
`NotSeekable`.

`MemoryReader` borrows its source and supports bounded seeking while open. Construction from temporary `std::string` and `std::vector<std::byte>`
storage is rejected. `MemoryWriter` owns append-only byte storage and exposes reservation, borrowed inspection, ownership transfer, and validated
UTF-8 copies.

## Whole-stream helpers

| API | Payload | Behavior |
| --- | --- | --- |
| `readAllBytes()` | `std::vector<std::byte>` | Drains a reader under a hard size limit. |
| `readAllText()` | `std::string` | Drains and validates strict UTF-8 (@ref io_utf8_policy). |
| `writeAllBytes()` | `Types::WriteProgress` | Retries successful short writes. |
| `writeAllText()` | `Types::WriteProgress` | Validates all input before writing. |

Read and write helpers preserve partial data or progress in a failed result when available. Scratch buffers are borrowed for the call, used only for
unknown-size readers, and never retained.

## Package boundary

IO is a static `GameWIP::IO` package. It publicly depends on `GameWIP::Status` for operation results and privately on `GameWIP::Unicode` for text
validation. Its public boundary includes standard-library containers, spans, strings, and virtual interfaces; public layouts and virtual function
order are compatibility-relevant.

## Related pages

- @ref io_reader_writer_contract
- @ref io_error_model
- @ref io_runtime_performance
- @ref io_examples
