@page io_public_api IO public API

Include `io/io.h` and link `IO`.

## Contract selection

Use `Reader` and `Writer` when an API needs a platform-neutral byte-transfer contract. They are movable, non-copyable interfaces with optional seek, size, flush, and close behavior. Unsupported optional operations return a status; callers must not infer support from the concrete type.

Use `MemoryReader` for non-owning reads over stable caller-owned storage. Temporary string and vector construction is rejected because it would create a dangling reader. Closing the reader changes its state but never affects the source storage.

Use `MemoryWriter` when output ownership and capacity reuse belong to the caller. It remains append-only, keeps collected bytes inspectable after close, and handles input views that alias its current storage.

Use the whole-stream helpers when partial backend transfers must be retried or a reader must be drained with a hard accepted-size limit. For unknown-size input, pass reusable scratch storage when allocation control matters.

## Flush modes

`Types::FlushMode` describes the requested flush strength. Use `isValidFlushMode()` when an API accepts a flush mode and must reject unknown enum values consistently. The validator is `constexpr` and non-throwing.

## Failure and allocation behavior

Expected failures use `Types::Status` and portable `Types::ErrorCode` values. Native codes and diagnostic messages are supplemental and are not stable machine-readable interfaces.

Whole-stream helpers preserve data transferred before a later failure where practical. Write-all helpers return `Types::WriteResult`, including progress made by the final failing call. `maxBytes` is a hard retained-data limit; an unknown-size reader may be probed by one byte to distinguish exact-limit end-of-stream from over-limit input.

Operations that return a status convert allocation failure to `OutOfMemory` where their contract permits it. Constructors, explicit reserve operations, and functions returning owning standard-library containers may still propagate allocation exceptions.

See @ref io_reader_writer_contract, @ref io_error_model, and @ref io_runtime_performance for detailed contracts.
