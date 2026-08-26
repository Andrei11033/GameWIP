@page io IO

`GameWIP::IO` is the platform-neutral byte-transfer contract shared by low-level GameWIP libraries.

It defines status and result types, abstract reader and writer interfaces, memory-backed implementations, and helpers that complete whole-stream byte
transfers and strict UTF-8 text transfers. IO does not open operating-system resources and has no platform backend.

## How the library is organized

`Reader` and `Writer` are the common byte-stream vocabulary. Concrete owners,
such as FileSystem handles, implement that vocabulary; `MemoryReader` and
`MemoryWriter` provide in-memory implementations. Single-operation methods may
make partial progress, while the higher-level helpers repeat those operations,
enforce limits, and return the accumulated result. Text helpers add strict UTF-8
validation without changing the underlying byte-transfer model.

## Consumer manual

- @subpage io_quick_start — Include, link, read, write, and inspect failures in
  a minimal program.
- @subpage io_public_api — Find the interfaces, implementations, options,
  results, constants, and whole-stream helpers.
- @subpage io_reader_writer_contract — Understand partial progress, position,
  size, seek, flush, and backend implementation rules.
- @subpage io_error_model — Interpret statuses, native diagnostics, progress,
  and exception boundaries.
- @subpage io_runtime_performance — Understand allocation, retry, scratch-space,
  and whole-stream performance behavior.
- @subpage io_examples — See memory-backed, bounded, and UTF-8 transfers in
  complete examples.
- @subpage io_troubleshooting — Diagnose stalled transfers, short writes,
  size-limit failures, invalid text, and unsupported capabilities.

## Maintainer validation

- @subpage io_testing — See the behavior matrix and validation commands.
- @subpage io_test_hooks — Understand source-tree-only fault injection and its
  reset rules.

## Generated API reference

Use @ref GameWIP::IO for active interfaces, implementations, constants, and helpers. Use @ref GameWIP::IO::Types for passive status, option, and
result types.

The generated reference documents every public declaration from `io/io.h`. The manual explains how those declarations work together, which contracts
backend implementations must preserve, and which caveats affect callers.

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

IO is installed as the static target `GameWIP::IO`. Unicode is its private
foundational text-correctness dependency and is resolved by the installed
package. The low-level Reader, Writer, and byte-transfer paths are
encoding-agnostic and perform no Unicode work.

FileSystem and Terminal consume IO contracts; IO must not depend on either of them.

IO intentionally has no `open()` API. Resource-owning libraries create their own concrete readers and writers, while memory-backed IO is constructed
directly.
