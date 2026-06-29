@page io IO

`GameWIP::IO` is the platform-neutral byte and text I/O contract library.

It defines shared reader, writer, status, error, and whole-stream helper APIs for low-level libraries. IO does not call the operating system and has no platform backend.

## User manual

- @subpage io_quick_start
- @subpage io_public_api
- @subpage io_reader_writer_contract
- @subpage io_error_model
- @subpage io_runtime_performance
- @subpage io_examples
- @subpage io_troubleshooting

## Developer validation

- @subpage io_testing

## Generated API reference

Use @ref GameWIP::IO for active interfaces and helpers, and @ref GameWIP::IO::Types for passive status, option, and result shapes. These generated pages document every public type, enum value, field, function, overload, and constant from `io/io.h`; the manual pages above explain how those symbols work together.

## Key behavior

- `Reader` and `Writer` define active byte-transfer contracts.
- Public status helpers create consistent success and failure statuses for IO consumers.
- `MemoryReader` is a non-owning reader over caller-owned contiguous bytes.
- `MemoryWriter` owns a growing byte vector and supports explicit capacity reuse or byte extraction.
- `readAllBytes()` and `readAllText()` optimize known-size readers and enforce a hard caller byte limit.
- `writeAllBytes()` and `writeAllText()` retry partial successful writes and report total accepted bytes.
- Expected I/O failures return `Types::Status`.
- Text helpers preserve UTF-8 bytes without validating or parsing them.

`GameWIP::IO` is the dependency root for FileSystem and Terminal. Those libraries depend on IO; IO must not depend on either of them.

IO intentionally has no `open()` API because it does not know which resource is being opened. Resource-owning libraries such as FileSystem and Terminal own open behavior. Memory-backed IO is created directly with constructors and uses `close()` / `isOpen()` state.
