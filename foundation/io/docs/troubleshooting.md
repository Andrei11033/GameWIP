@page io_troubleshooting Troubleshooting

## A whole-stream read returns `SizeLimitExceeded`

`maxBytes` is a hard accepted-size limit. It does not request truncation.

For a known-size reader, the remaining size exceeded the limit or the result container's representational range. For an unknown-size reader, the helper observed an additional byte beyond the accepted limit. That probe byte is consumed but not stored.

## An exact-limit unknown-size read returns `ReadFailed`

After producing the accepted bytes, the reader must eventually report end-of-stream. A successful zero-byte read with `endOfStream = false` provides no progress and cannot be retried safely.

## A known-size read returns `PartialRead`

The reader reported more remaining bytes through `size()` and `position()` than it produced before end-of-stream. Ensure those capability queries describe the same stream state as `read()`.

## A whole-stream read fails before calling `read()`

Supported size or position queries failed. Return `NotSeekable` or `Unsupported` when a capability is intentionally unavailable and the helper should use the unknown-size path. Other failures are propagated.

A position greater than the reported size returns `InvalidArgument`.

## A scratch buffer appears unused

Known-size readers are read directly into final output storage. Scratch storage is used only when size or position returns `NotSeekable` or `Unsupported`.

## A reader loses one byte after an over-limit result

Unknown-size limit enforcement uses a one-byte probe. When more input exists, the probe consumes one byte and returns `SizeLimitExceeded`. Use a backend-specific lookahead or buffering layer when the caller must recover the complete stream after rejecting oversized input.

## MemoryReader returns incorrect data

`MemoryReader` does not own its source. Keep the source alive and at a stable address. Do not resize, reassign, move, or destroy the source container while the reader is in use.

Direct temporary string and vector construction is rejected, but a dangling caller-created span or string view cannot be detected.

## MemoryReader or MemoryWriter returns `NotOpen`

The object was closed. Close is idempotent, but memory objects cannot be reopened.

MemoryWriter output remains inspectable, clearable, reservable, and extractable after close. New writes, flushes, and position queries fail with `NotOpen`.

## A retained `MemoryWriter::bytes()` view is invalid

The view refers to writer-owned storage. A write or reserve may reallocate it. Clear, take, move, destruction, or ownership transfer makes the previous logical view unusable. Copy the bytes or call `takeBytes()` when longer ownership is required.

## `writeAllBytes()` returns `WriteFailed` after making progress

The writer either reported more bytes than were supplied or returned successful zero progress while input remained. Both violate the Writer contract.

A legitimate failure may still carry nonzero `bytesWritten`; that count includes valid progress from the final failing call.

## An exception escapes a whole-stream helper

Generic helpers convert their own documented allocation failures, but they do not catch arbitrary exceptions from custom Reader or Writer implementations. Backends should use statuses for expected I/O failures and document any exceptional programming or allocation failures they permit.

## Memory usage is higher than expected

For repeated unknown-size reads, reuse a scratch buffer. For repeated writes, reserve an expected capacity and call `clear()` between operations. Use `takeBytes()` when the caller should own the vector and accept that writer capacity may be discarded.

## Text helpers accept invalid UTF-8

IO treats text as bytes. It preserves embedded NUL and invalid byte sequences. Validation, normalization, decoding, and parsing belong to a higher-level library.

## Related pages

- @ref io_reader_writer_contract
- @ref io_error_model
- @ref io_runtime_performance
