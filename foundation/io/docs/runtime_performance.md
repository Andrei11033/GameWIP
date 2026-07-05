@page io_runtime_performance IO runtime and performance

IO itself performs no operating-system calls. Blocking behavior comes from the concrete `Reader` or `Writer` implementation.

## Whole-stream reads

When both `Reader::size()` and `Reader::position()` succeed, `readAllBytes()` and `readAllText()` calculate the known remaining byte count and read directly into final output storage. This avoids a temporary transfer buffer and allocates the final result once.

If size or position is unsupported, the helpers use the unknown-size path. The normal overload allocates one temporary transfer buffer without pre-filling it; the scratch-buffer overload reuses caller-owned storage and avoids that allocation.

Unknown-size output grows geometrically through `std::vector` or `std::string`. Reusing a scratch buffer removes temporary-buffer churn, but the returned result still owns its collected bytes.

## Hard byte limits

`maxBytes` is a hard maximum accepted stream size, not a truncation request.

Known-size readers are rejected before output allocation when their remaining byte count exceeds the limit.

Unknown-size readers may require a one-byte probe after collecting exactly `maxBytes` bytes. End-of-stream makes the read successful; another byte returns `SizeLimitExceeded`. That probe advances the reader by one additional byte when the stream exceeds the limit, but the returned result never stores more than `maxBytes` bytes.

## MemoryReader

`MemoryReader` performs bounded, overlap-safe transfers from caller-owned contiguous storage. It does not allocate.

The caller must keep the source bytes alive and at a stable address for the reader's full lifetime.

## MemoryWriter

`MemoryWriter` uses a growing `std::vector<std::byte>`.

- Use the initial-capacity constructor or `reserve()` when the expected output size is known.
- `clear()` removes bytes while preserving capacity for reuse.
- `takeBytes()` transfers the owned vector out, replaces writer storage with an empty vector, and may discard reserved capacity.
- Writes from a span that aliases current writer bytes are supported without allocating a temporary copy.
- `text()` creates a separate `std::string` copy.

## Status allocation behavior

IO-generated statuses leave `Status::message` empty. Concrete backends may add diagnostic text when the extra allocation and detail are appropriate.

Status-returning helpers report failed allocations as `OutOfMemory`. `SizeLimitExceeded` remains reserved for explicit or representational size limits.

`Status` intentionally retains its owning diagnostic string. This makes successful result objects larger than a code-only status type, but preserves backend diagnostics without a second error channel. Hot paths should prefer empty code-only statuses and avoid constructing messages on success.

## Threading

Different Reader or Writer objects may be used concurrently.

The same object is not thread-safe unless its concrete implementation explicitly says otherwise. `MemoryReader` and `MemoryWriter` are not internally synchronized.
