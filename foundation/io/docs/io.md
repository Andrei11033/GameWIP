@page io IO

`GameWIP::IO` is the platform-neutral byte-transfer contract shared by low-level GameWIP libraries.

It defines status and result types, abstract reader and writer interfaces, memory-backed implementations, and helpers that complete whole-stream byte transfers and strict UTF-8 text transfers. IO does not open operating-system resources and has no platform backend.

## Consumer manual

- @subpage io_quick_start
- @subpage io_public_api
- @subpage io_reader_writer_contract
- @subpage io_error_model
- @subpage io_runtime_performance
- @subpage io_examples
- @subpage io_troubleshooting

## Maintainer validation

- @subpage io_testing
- @subpage io_test_hooks

## Generated API reference

Use @ref GameWIP::IO for active interfaces, implementations, constants, and helpers. Use @ref GameWIP::IO::Types for passive status, option, and result types.

The generated reference documents every public declaration from `io/io.h`. The manual explains how those declarations work together, which contracts backend implementations must preserve, and which caveats affect callers.

## Key behavior

- `Reader` and `Writer` are movable, non-copyable byte-transfer interfaces.
- Optional capabilities use statuses rather than separate interface hierarchies.
- `MemoryReader` is a non-owning, seekable view over stable caller-owned storage.
- `MemoryWriter` owns append-only storage that can be inspected, reused, or transferred.
- Checked interface and helper operations are `noexcept`; expected failures are returned through status-bearing results.
- Whole-stream read helpers optimize readers that report both size and position.
- Unknown-size reads enforce hard byte limits and support reusable caller-owned scratch storage.
- Whole-stream write helpers retry successful short writes and preserve progress reported with a later failure.
- Text helpers enforce strict UTF-8, preserve embedded NUL, and perform no normalization, BOM transformation, or parsing.

## Dependency boundary

Unicode is the foundational text-correctness dependency used by IO's text helpers. The low-level Reader, Writer, and byte-transfer paths remain encoding-agnostic and perform no Unicode work.

FileSystem and Terminal consume IO contracts; IO must not depend on either of them.

IO intentionally has no `open()` API. Resource-owning libraries create their own concrete readers and writers, while memory-backed IO is constructed directly.
