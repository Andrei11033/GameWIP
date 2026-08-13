@page io_reader_writer_contract Reader and writer contract

`Reader` and `Writer` are extension interfaces for resource-owning libraries. This page owns the transfer, capability, lifetime, exception, and concurrency rules that implementations and generic callers must preserve.

## Interface model

Both interfaces are movable and non-copyable. Their destructors are virtual and non-throwing.

The only required virtual operation is `Reader::read()` or `Writer::write()`. Base implementations provide neutral defaults for optional behavior.

### Reader defaults

| Operation | Base behavior |
| --- | --- |
| `isOpen()` | Returns `true`. |
| `canSeek()` | Returns `false`. |
| `close()` | Successful no-op. |
| `position()` | Returns `NotSeekable`. |
| `size()` | Returns `NotSeekable`. |
| `seek()` | Returns `NotSeekable`. |

### Writer defaults

| Operation | Base behavior |
| --- | --- |
| `isOpen()` | Returns `true`. |
| `canSeek()` | Returns `false`. |
| `flush()` | Rejects unknown modes, otherwise succeeds without requiring physical I/O. |
| `close()` | Successful no-op. |
| `position()` | Returns `NotSeekable`. |
| `seek()` | Returns `NotSeekable`. |

These defaults are suitable for stateless streaming adapters. Resource-owning implementations should override state and lifecycle operations that have meaningful behavior.

## Read contract

`read(destination)` may copy from zero through `destination.size()` bytes.

An implementation must:

- Never report `bytesRead > destination.size()`.
- Report valid copied bytes even when the same call also reports failure.
- Set `endOfStream` according to whether no additional input remains after the call.
- Return a successful zero-byte result only for an empty request or when end-of-stream is reported.
- Avoid retaining the destination span after the call returns unless a separate public contract explicitly transfers or extends its lifetime.

The final non-empty read may set `endOfStream = true`. Callers must inspect both fields.

## Write contract

`write(bytes)` may accept from zero through `bytes.size()` bytes.

An implementation must:

- Never report `bytesWritten > bytes.size()`.
- Report valid accepted bytes even when the same call also reports failure.
- Return success with zero bytes for an empty request.
- Avoid returning success with zero bytes while non-empty input remains, because generic retry loops cannot make progress.
- Avoid retaining the input span after the call returns unless a separate public contract explicitly transfers or extends its lifetime.

A successful short write means progress was made and the remaining suffix may be retried. A writer may instead accept a prefix and return a failure in the same call; generic helpers preserve that accepted count and stop.

## Optional capabilities

`canSeek()` is an advisory query. Callers must still inspect the result of `position()`, `size()`, and `seek()` because runtime state may change and size support is not implied by seek support.

Whole-stream read helpers do not rely on `canSeek()`. They query `size()` and `position()` directly:

- If `size()` returns `NotSeekable` or `Unsupported`, the reader is treated as unknown-size.
- If size succeeds but position returns either of those codes, the known size is discarded and the unknown-size path is used.
- Other capability-query failures are propagated before payload reads.
- A reported position greater than size is an invalid contract and returns `InvalidArgument`.

A backend should use `NotSeekable` for stream-position capabilities and `Unsupported` for other intentionally absent capabilities.

## Whole-stream reads

`readAllBytes()` and `readAllText()` start at the reader's current position. `readAllBytes()` is encoding-agnostic. `readAllText()` validates the collected range as strict UTF-8 before returning it.

### Known-size path

When both size and position succeed, the helpers calculate the remaining byte count and:

- Reject counts above `maxBytes` or the result container's representational limit before reading.
- Allocate the final output to the known remaining size.
- Read until that exact count is produced or a failure occurs.
- Preserve bytes produced by a final failing read.
- Return `PartialRead` if end-of-stream occurs before the promised count.
- Return `ReadFailed` for an impossible byte count or zero progress before completion.

An exact known byte count is authoritative; the final read does not need to set `endOfStream` once the promised count has been produced.

### Unknown-size path

When size or position is unavailable, the helpers read through either an internally allocated buffer or caller-owned scratch storage.

They:

- Require a nonzero internal buffer size or non-empty scratch span.
- Append valid bytes before processing a failure from the same read.
- Stop successfully when `endOfStream` is reported.
- Return `ReadFailed` for impossible counts or successful zero progress without end-of-stream.
- Never retain the scratch span after the call returns.

The scratch buffer is ignored when the reader qualifies for the known-size path.

### UTF-8 text finalization

`readAllText()` never exposes malformed bytes through its `text` field.

- Completely valid input preserves the ordinary read status and full text.
- Malformed UTF-8 returns `EncodingFailed` and preserves only the complete valid prefix.
- An incomplete suffix at a definitive stream end returns `EncodingFailed` and preserves only the complete valid prefix.
- An incomplete suffix accompanying an existing non-EOF I/O or size-limit failure is trimmed while that existing failure remains primary, because additional continuation bytes may have existed beyond the failed boundary.
- If a known-size reader reaches end-of-stream early and the produced prefix ends with an incomplete sequence, `EncodingFailed` takes precedence over `PartialRead`.
- Validation performs no normalization, BOM transformation, replacement, or repair.

### Hard byte limits

`maxBytes` is the maximum accepted output size, not a truncation request.

For an unknown-size reader, reaching the limit requires a one-byte probe:

- End-of-stream makes the operation successful.
- One additional byte returns `SizeLimitExceeded` and consumes that byte without storing it.
- A zero-byte backend failure is propagated.
- A zero-byte success without end-of-stream returns `ReadFailed`.

A zero limit accepts an empty stream and probes unknown-size input immediately.

## Whole-stream writes

`writeAllBytes()` repeatedly passes the unwritten suffix to the writer until all input is accepted or a call fails.

It:

- Succeeds for empty input without calling `write()`.
- Retries successful short writes.
- Includes bytes accepted by a final failing call in `bytesWritten`.
- Returns `WriteFailed` if a writer reports an impossible count or successful zero progress.
- Does not flush or close the writer.

`writeAllText()` first validates the complete string view as strict UTF-8. Malformed or incomplete input returns `EncodingFailed` with `bytesWritten == 0` and does not call `Writer::write()`. Valid text then follows the same retry/progress contract as `writeAllBytes()`, including preservation of embedded NUL bytes. It performs no normalization, BOM transformation, flush, or close.

## MemoryReader

`MemoryReader` is a non-owning view over contiguous bytes.

- The source must remain alive and at a stable address while the reader is used.
- The `std::string_view` constructor treats character storage only as bytes; it performs no encoding interpretation or validation.
- Direct temporary `std::string` and byte-vector construction is deleted.
- Caller-created dangling spans and string views remain the caller's responsibility.
- Reads use overlap-safe copying, so the destination may overlap the source.
- Seeking is bounded to the inclusive range from position zero through end-of-stream.
- An invalid origin returns `InvalidArgument`; an out-of-range target returns `SeekFailed`.
- `close()` is idempotent and never modifies the source.
- After close, read, size, position, and seek operations return `NotOpen`; `canSeek()` returns false.

## MemoryWriter

`MemoryWriter` owns append-only `std::vector<std::byte>` storage.

- Writes from a valid subspan of the writer's current `bytes()` view are supported, including when appending reallocates storage.
- `flush()` validates its mode and otherwise performs no physical work.
- `close()` is idempotent and preserves collected output.
- After close, `write()`, `flush()`, and `position()` return `NotOpen`.
- `bytes()`, `copyText()`, `takeBytes()`, `size()`, `capacity()`, `empty()`, `reserve()`, and `clear()` remain available after close.
- `clear()` preserves capacity.
- `takeBytes()` transfers ownership, leaves the writer empty, preserves open/closed state, and may discard reserved capacity.

`bytes()` returns a non-owning view into writer-owned storage. Do not retain it across operations that can modify storage or ownership. A write or reserve may reallocate; clear, take, move assignment, destruction, and ownership transfer invalidate the prior logical view.

`copyText()` returns a `CopyTextResult` containing an independent strict UTF-8 copy and preserves embedded NUL bytes. Malformed or incomplete collected bytes return `EncodingFailed` with empty text. `reserve()` and `copyText()` report allocation, representation-limit, and unexpected failures through status.

## Exceptions

Every virtual transfer, lifecycle, and checked capability operation is `noexcept`.

- Custom `Reader` and `Writer` overrides must contain expected backend and allocation failures and return the most specific status available.
- Throwing from an override violates the interface contract and terminates the process through the language `noexcept` rule; generic helpers cannot recover from that violation.
- Whole-stream helpers translate their own allocation failures to `OutOfMemory`, representation failures to `SizeLimitExceeded`, and unexpected internal failures to `Unknown`.
- `MemoryWriter::write()`, `reserve()`, and `copyText()` use the same translation categories and never expose a partially mutated result after an injected allocation or length failure.
- Destructors do not throw. Call `close()` explicitly when close failure must be observed.

Argument construction still occurs before function entry. For example, creating an owning diagnostic string for `makeStatus()` may throw before its `noexcept` boundary; build optional diagnostics best-effort when operating in a non-throwing backend.

## Thread-safety and blocking

Different reader or writer objects may be used concurrently. The same object is not thread-safe unless its concrete implementation explicitly says otherwise.

IO itself performs no operating-system calls. Blocking behavior belongs to the concrete backend. `MemoryReader` and `MemoryWriter` are not internally synchronized and do not block on operating-system I/O.

## Non-goals

IO does not provide file opening, terminal access, network I/O, asynchronous I/O, memory mapping, file watching, parsing, or format decoding. Resource-owning libraries implement those concerns behind the shared contracts.

## Related pages

- @ref io_public_api
- @ref io_error_model
- @ref io_runtime_performance
- @ref io_examples
