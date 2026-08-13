@page terminal_segmented_writes Segmented writes

Segmented writes emit one logical batch containing plain text, styled text, and—on suitable endpoints—raw bytes without requiring the caller to concatenate one owning string first.

## Segment construction

`Types::Output::SegmentKind` contains `Text`, `StyledText`, and `Bytes`. Construct values with `textSegment()`, `styledTextSegment()`, and `byteSegment()`.

A segment is valid by construction. Its payload accessors are interpreted according to `kind()`:

- `text()` is meaningful for `Text` and `StyledText`;
- `style()` is meaningful for `StyledText`;
- `bytes()` is meaningful for `Bytes`.

`Types::Style::Request` is copied into a styled segment. Text and byte payloads are non-owning views. Copying a segment copies those views, not the referenced storage.

The source storage must remain alive and unchanged until `writeSegments()` returns. Factory overloads delete common temporary-owning-string and non-borrowed temporary-byte-range cases, but lvalue lifetime remains the caller's responsibility.

## Batch options

`Types::Output::SegmentOptions` contains:

- `styleMode` for styled segments;
- `appendLineEnding` after the complete batch;
- `lineEnding` used when appending;
- `flushMode` applied after emission.

Per-segment style remains in each segment; there is no global style object for the batch.

## Validation and endpoint behavior

Terminal validates the complete batch before normal emission. Invalid enum values, unavailable required styles, or a byte segment targeting a real Win32 console fail before earlier segments are intentionally written.

Validation and assembly can allocate temporary storage. Allocation failure can therefore stop the operation before the platform write. Once platform emission begins, the operation is not transactional: a failing endpoint can emit a prefix.

Redirected endpoints can accept mixed text and byte segments when their capabilities allow byte output. Real Win32 consoles reject raw byte segments with `Unsupported` because arbitrary bytes are not valid console text.

Plain text-only batches skip unnecessary capability work. Large temporary assembly capacity is not retained indefinitely.

## Empty batches and flushing

An empty batch is valid. It can still append the requested line ending or perform the requested flush according to `Types::Output::SegmentOptions`.

A flush failure can occur after the full assembled batch was accepted. `writeSegments()` returns a status rather than a byte count, so applications requiring byte-level progress should use `writeBytes()` for that transfer shape.

## Concurrency

One `writeSegments()` call is one Terminal serialization unit for the selected stream. It does not coordinate with `std::cout`, `std::cerr`, `printf`, native writes, or third-party terminal APIs.

See @ref terminal_read_write and @ref terminal_styling.
