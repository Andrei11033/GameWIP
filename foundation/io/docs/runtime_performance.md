@page io_runtime_performance Runtime and performance

This page explains where IO allocates, when it can use a known stream size, how
scratch storage is reused, and when data is copied or moved. The transfer
contracts themselves are in @ref io_reader_writer_contract.

## Known-size reads

When both `Reader::size()` and `Reader::position()` succeed, read-all helpers calculate the remaining byte count and allocate the final vector or
string directly.

This path:

- Avoids a separate temporary transfer allocation.
- Rejects caller and representation limits before allocating.
- Starts from the current reader position rather than assuming position zero.
- Ignores the caller scratch buffer because direct destination reads are available.

The final output is resized before reading, then reduced to valid progress if a later failure occurs.

## Unknown-size reads

The ordinary overload allocates one temporary buffer with an effective size of `min(bufferSize, maxBytes)`. `kDefaultBufferSize` is used when the
caller does not supply a size.

The scratch-buffer overload reuses caller-owned temporary storage and avoids that allocation. The returned vector or string still owns and grows its
output storage.

Unknown-size output grows according to the standard container's allocation strategy. Reusing scratch storage controls temporary-buffer churn but does
not preallocate the final unknown-size result.

## Text validation

`readAllText()` performs one strict validation pass over the collected string (@ref io_utf8_policy). Byte-oriented reads perform no Unicode work.

`writeAllText()` validates its input once before the first writer call, so invalid input cannot partially mutate the destination.

The text helpers intentionally do not cache validation state or introduce a validated-string wrapper. Whole-text operations are trust boundaries;
ordinary byte transfers remain unchanged.

## Hard limits and probing

`maxBytes` is a hard accepted-size limit; probe and rejection rules: @ref io_reader_writer_contract. Known-size readers are rejected before output
allocation.

`kNoByteLimit` removes only the caller-imposed limit. Container `max_size()`, address-space, and allocation limits still apply and may produce
`SizeLimitExceeded` or `OutOfMemory`.

## MemoryReader

`MemoryReader` performs bounded `memmove` transfers from caller-owned storage. It allocates nothing and supports overlapping source and destination
ranges.

Its performance depends on keeping the source alive and stable; it never copies the source for ownership safety.

## MemoryWriter

`MemoryWriter` uses a growing `std::vector<std::byte>`.

- Call checked `reserve()` before writing when output size is predictable.
- Use `clear()` to reuse capacity between operations.
- Use `takeBytes()` when ownership should move to the caller; the writer may lose its reserved capacity.
- `bytes()` is zero-copy but returns a borrowed view into writer-owned storage.
- `copyText()` validates first, then allocates and copies; invalid input allocates no result string.
- Appending a valid subspan of current writer storage is handled without a separate temporary copy.

## Status cost

`Status` owns an optional `std::string`. IO-generated statuses normally leave it empty. Backends should avoid constructing success messages and should
attach failure text only when the diagnostic value justifies the allocation.

Code-only statuses remain useful in hot or allocation-sensitive paths, while `nativeCode` and `message` preserve backend detail when needed.

## Benchmarks

The `io` benchmark module provides stable `BM_IO_*` registrations for fixed-size `MemoryReader` reads, pre-reserved `MemoryWriter` writes, and the
known-size `readAllBytes()` path. Run optimized measurements through @ref project_benchmarking; correctness and failure translation remain owned by
the focused test module.

## Related pages

- @ref io_reader_writer_contract
- @ref io_examples
- @ref io_troubleshooting
