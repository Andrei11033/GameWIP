@page io_troubleshooting IO troubleshooting

## `readAllBytes()` or `readAllText()` returns `SizeLimitExceeded`

`maxBytes` is a hard accepted-size limit. It does not request silent truncation.

For a known-size reader, the remaining size exceeded the limit. For an unknown-size reader, the helper observed data beyond the limit, possibly through the one-byte limit probe.

## An exact-limit unknown-size read returns `ReadFailed`

After producing the final bytes, the Reader must eventually report end-of-stream. A successful read that repeatedly returns zero bytes without end-of-stream is invalid because whole-stream helpers cannot make progress.

## A known-size read returns `PartialRead`

The Reader reported a remaining size larger than the bytes it produced before end-of-stream. Check that `size()` and `position()` describe the same stream state as `read()`.

## A whole-stream read fails before calling `read()`

Failures from supported `size()` or `position()` queries are propagated. Return `NotSeekable` or `Unsupported` when the capability is unavailable and the helper should use the unknown-size path.

## MemoryReader returns incorrect data

`MemoryReader` is non-owning. Ensure the source span, string view, or vector remains alive and does not move while the reader is used. Direct temporary `std::string` and vector construction is rejected, but a caller-created dangling span or string view cannot be detected.

## MemoryReader or MemoryWriter returns `NotOpen`

The object was closed. Closing a memory object is idempotent, but it cannot be reopened.

`MemoryWriter` output remains inspectable and extractable after close; new writes, flushes, and stream-position queries return `NotOpen`.

## Memory usage is higher than expected

For repeated unknown-size reads, reuse the scratch-buffer overload. For repeated writes, reserve an expected capacity and use `clear()` between operations. Use `takeBytes()` when ownership should move to the caller.
