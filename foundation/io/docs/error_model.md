@page io_error_model Error model

IO operations use the shared `GameWIP::Status::Types::Result<T>` contract. The portable `Code` describes the outcome; optional diagnostics
provide native codes and developer-facing UTF-8 text.

## Reading a result

```cpp
auto result = GameWIP::IO::readAllBytes(reader, 1024);
if(!result.ok())
{
    const auto code = result.status().code();
    if(result.hasValue())
    {
        // Keep or discard the partial bytes according to the caller's policy.
        consume(result.value());
    }
}
```

`ok()` means the operation succeeded and a value is present. Failed results may retain useful partial progress. `Result<void>` has no value and
reports success from its status alone. Do not call `value()` unless `hasValue()` is true.

For reporting, inspect `status().diagnostic()` when present. `Status::code()` is the only field used for portable control flow. Native code includes
its source domain; diagnostic text is UTF-8, optional, and not stable for parsing. Code-only statuses do not allocate diagnostic storage.

## Choosing a code

Use the most specific shared `GameWIP::Status::Types::Code` that describes the operation. Common IO codes include:

| Code | Use |
| --- | --- |
| `InvalidArgument` | An argument or backend result violates the contract. |
| `Unsupported` | An operation or capability is unavailable. |
| `NotOpen` | The operation requires an open resource. |
| `NotSeekable` | Position or seek behavior is unavailable. |
| `PermissionDenied` | Access policy rejects the operation. |
| `ReadFailed`, `WriteFailed`, `FlushFailed`, `CloseFailed`, `SeekFailed` | The named operation failed without a more specific code. |
| `PartialRead` | A known-size reader stopped before its promised byte count. |
| `SizeLimitExceeded` | A caller limit or supported size is exceeded. |
| `OutOfMemory` | An internal allocation failed. |
| `EncodingFailed` | Strict UTF-8 validation failed. |

The complete vocabulary and symbolic names are documented by the Status library. Enum numeric values are not stable IDs or wire-format values.

## Partial progress and end-of-stream

- A failed read or write may include progress in its value.
- Whole-stream helpers preserve accumulated data when a later read fails.
- A successful short write is retried by `writeAllBytes()`; successful zero progress while bytes remain becomes `WriteFailed`.
- `ReadProgress::endOfStream` is independent of `bytesRead`. The final read may return bytes and set it, or return zero bytes at EOF.
- Known-size early EOF becomes `PartialRead`. Unknown-size zero progress without EOF becomes `ReadFailed`.
- At a finite limit, an unknown-size reader may be advanced by one probe byte that is not stored.

@anchor io_utf8_policy

## UTF-8 text policy

This is the shared policy for IO, FileSystem, and Terminal text APIs; other pages link here.

- Text is strict UTF-8. No normalization, BOM transformation, replacement, or repair is performed. Embedded NUL is preserved.
- On failure, text results may keep only the complete valid UTF-8 prefix.
- Malformed bytes return `EncodingFailed`, including when a backend failure occurs in the same read.
- An incomplete suffix returns `EncodingFailed` at definitive end-of-input. If a backend or size limit stops the read first, that failure remains
  primary and the incomplete suffix is trimmed.
- Text writes validate all input before the first write.
- `MemoryReader`'s `std::string_view` constructor is byte-oriented and does not validate encoding.

## Failure containment

Reader, Writer, MemoryReader, and MemoryWriter checked methods are `noexcept`. Implementations must convert expected failures into results; throwing
from an override violates the contract and terminates the process.

Whole-stream and memory-writer operations map allocation failures to `OutOfMemory`, size representation failures to `SizeLimitExceeded`, and
unexpected exceptions to `Unknown`.

## Related pages

- @ref io_reader_writer_contract
- @ref io_runtime_performance
- @ref io_troubleshooting
