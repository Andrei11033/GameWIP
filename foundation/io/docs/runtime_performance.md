@page io_runtime_performance IO runtime and performance

This page owns allocation, buffering, and data-movement characteristics. Transfer correctness and backend obligations are documented in @ref io_reader_writer_contract.

## Known-size reads

When both `Reader::size()` and `Reader::position()` succeed, read-all helpers calculate the remaining byte count and allocate the final vector or string directly.

This path:

- Avoids a separate temporary transfer allocation.
- Rejects caller and representation limits before allocating.
- Starts from the current reader position rather than assuming position zero.
- Ignores the caller scratch buffer because direct destination reads are available.

The final output is resized before reading, then reduced to valid progress if a later failure occurs.

## Unknown-size reads

The ordinary overload allocates one temporary buffer with an effective size of `min(bufferSize, maxBytes)`. `kDefaultBufferSize` is used when the caller does not supply a size.

The scratch-buffer overload reuses caller-owned temporary storage and avoids that allocation. The returned vector or string still owns and grows its output storage.

Unknown-size output grows according to the standard container's allocation strategy. Reusing scratch storage controls temporary-buffer churn but does not preallocate the final unknown-size result.

## Hard limits and probing

`maxBytes` is a hard accepted-size limit.

Known-size readers are rejected before output allocation when the remaining count exceeds the limit. Unknown-size readers may consume one additional probe byte after collecting exactly the limit. The probe distinguishes exact end-of-stream from over-limit input; the extra byte is never stored.

`kNoByteLimit` removes only the caller-imposed limit. Container `max_size()`, address-space, and allocation limits still apply and may produce `SizeLimitExceeded` or `OutOfMemory`.

## MemoryReader

`MemoryReader` performs bounded `memmove` transfers from caller-owned storage. It allocates nothing and supports overlapping source and destination ranges.

Its performance depends on keeping the source alive and stable; it never copies the source for ownership safety.

## MemoryWriter

`MemoryWriter` uses a growing `std::vector<std::byte>`.

- Use the initial-capacity constructor or `reserve()` when output size is predictable.
- Use `clear()` to reuse capacity between operations.
- Use `takeBytes()` when ownership should move to the caller; the writer may lose its reserved capacity.
- `bytes()` is zero-copy but returns a temporary view into writer-owned storage.
- `text()` allocates and copies the complete byte sequence.
- Appending a valid subspan of current writer storage is handled without a separate temporary copy.

## Status cost

`Status` owns an optional `std::string`. IO-generated statuses normally leave it empty. Backends should avoid constructing success messages and should attach failure text only when the diagnostic value justifies the allocation.

Code-only statuses remain useful in hot or allocation-sensitive paths, while `nativeCode` and `message` preserve backend detail when needed.

## Related pages

- @ref io_reader_writer_contract
- @ref io_examples
- @ref io_troubleshooting
