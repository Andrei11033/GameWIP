@page io_public_api Public API

Include `io/io.h`. Installed consumers link `GameWIP::IO`; source-tree consumers link `IO`. See @ref io_quick_start for complete CMake usage.

## Constants

| API | Purpose |
| --- | --- |
| `kNoByteLimit` | Removes the caller-imposed limit from a whole-stream read. Result-container and address-space limits still apply. |
| `kDefaultBufferSize` | Default temporary transfer-buffer size for the internally allocated unknown-size read path. |

## Status, option, and result types

The `GameWIP::IO::Types` namespace contains passive value types:

| Type | Contract |
| --- | --- |
| `ErrorCode` | Portable status category shared by IO and resource-owning libraries. |
| `Status` | Fields `code`, `nativeCode`, and UTF-8 diagnostic `message`; `ok()` is true only for `Success`. |
| `SeekOrigin` | `Begin`, `Current`, or `End` origin for signed seek offsets. |
| `FlushMode` | `None`, `Data`, or `DataAndMetadataBestEffort`. |
| `ReadResult` | Fields `status`, `bytesRead`, and independent `endOfStream` state. |
| `WriteResult` | Fields `status` and accepted-byte count `bytesWritten`. |
| `PositionResult` | Fields `status` and current byte `position`. |
| `SizeResult` | Fields `status` and total byte count `sizeBytes`. |
| `ReadAllBytesResult` | Fields `status` and collected binary `bytes`. |
| `ReadAllTextResult` | Fields `status` and valid UTF-8 `text`; failures preserve only a complete valid prefix. |
| `CopyTextResult` | Fields `status` and an owning validated UTF-8 `text` copy. |

A nonzero transfer count may accompany a failure status. `ReadResult::endOfStream` is independent of `bytesRead` and may be true on the final
non-empty read.

## Status helpers

| API | Purpose |
| --- | --- |
| `isValidFlushMode()` | Validates defined `FlushMode` enumerators. It is `constexpr` and non-throwing. |
| `makeStatus()` | Creates a status from portable, native, and diagnostic details. |
| `successStatus()` | Creates a successful status. |
| `errorCodeName()` | Returns a stable symbolic name for a known error code, or `"Unknown"` for an unknown enumerator. |

@ref io_error_model owns error-code selection and diagnostic rules.

## Reader interface

`Reader` is the platform-neutral input contract. It is movable, non-copyable, and has one required operation:

```cpp
virtual Types::ReadResult read(std::span<std::byte> destination) noexcept = 0;
```

The base class supplies defaults for `isOpen()`, `canSeek()`, `close()`, `position()`, `size()`, and `seek()`. This lets a stateless streaming adapter
implement only `read()` while resource-owning readers override the capabilities they support.

`MemoryReader` is the provided concrete reader. It accepts a byte span, character-backed string view, or lvalue byte vector; rejects direct temporary
string/vector construction; supports bounded seeking while open; and never owns or modifies its source. The string-view constructor is still
byte-oriented and performs no encoding interpretation.

## Writer interface

`Writer` is the platform-neutral output contract. It is movable, non-copyable, and has one required operation:

```cpp
virtual Types::WriteResult write(std::span<const std::byte> bytes) noexcept = 0;
```

The base class supplies defaults for `isOpen()`, `canSeek()`, `flush()`, `close()`, `position()`, and `seek()`.

`MemoryWriter` is the provided concrete writer. It owns append-only byte storage and exposes:

- Span and vector write overloads.
- Checked capacity reservation and reuse through `reserve()`.
- Non-owning byte inspection through `bytes()`.
- Checked owning UTF-8 copies through `copyText()`.
- Ownership transfer through `takeBytes()`.
- `size()`, `capacity()`, `empty()`, and `clear()` state access.

Collected output remains available after `close()`, but writing, flushing, and position queries require open state.

Every checked Reader, Writer, MemoryReader, MemoryWriter, and whole-stream operation is `noexcept`. Extension implementations must translate expected
failures into statuses and must not let exceptions escape an override.

## Whole-stream helpers

| API | Overloads | Behavior |
| --- | --- | --- |
| `readAllBytes()` | Internal buffer or caller-owned scratch buffer. | Drains a reader into `std::vector<std::byte>` with a hard accepted-size limit. |
| `readAllText()` | Internal buffer or caller-owned scratch buffer. | Drains a reader into valid UTF-8 `std::string`, returning `EncodingFailed` for malformed or definitively incomplete input. |
| `writeAllBytes()` | Span or byte vector. | Retries successful short writes until complete or failed. |
| `writeAllText()` | String view. | Validates the complete UTF-8 input before the first write, then writes all bytes including embedded NUL. |

The scratch buffer is used only for unknown-size readers. When both size and position are available, read-all helpers allocate the final output
directly and do not use the scratch storage.

@ref io_reader_writer_contract owns exact transfer and failure behavior. @ref io_runtime_performance owns allocation and limit details.

## Package and binary boundary

IO is installed as a static `GameWIP::IO` target with the public header `io/io.h`. It has no shared-library export surface. Its implementation depends
on `GameWIP::Unicode`; the installed IO package resolves that dependency automatically, so consumers still request and link only `GameWIP::IO`.

Its public boundary includes C++ standard-library containers, spans, strings, and virtual interfaces. Consumers therefore follow the project compiler,
standard-library, and exact-version compatibility policy documented in @ref project_library_compatibility. Public type layout, virtual function order,
declarations, and inline/template behavior are compatibility-relevant even though IO is static.

The source-tree target name, implementation source, validation code, and internal implementation namespaces are not installed interfaces.

## Related pages

- @ref io_reader_writer_contract
- @ref io_error_model
- @ref io_runtime_performance
- @ref io_examples
