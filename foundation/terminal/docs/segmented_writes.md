@page terminal_segmented_writes Terminal segmented writes

This page documents optimized segmented output behavior.

The current implementation supports plain text segments, styled text segments, byte segments where byte output is supported, batch line endings, and batch flush. Styled segments follow the batch `StyleMode`.

## Purpose

Segmented writes let callers write a mixed batch of text, styled text, and bytes without concatenating strings first.

The optimized path is:

```cpp
GameWIP::Terminal::writeSegments(std::span<const GameWIP::Terminal::Types::WriteSegment>{segments});
```

Logger and other performance-sensitive tools should prefer segmented writes when they need to compose colored prefixes, plain message text, byte fragments, and trailing line endings.

## Segment kinds

`Types::WriteSegmentKind` contains:

- `Text`;
- `StyledText`;
- `Bytes`.

Construct segments with:

- `textSegment(text)`;
- `styledSegment(text, style)`;
- `byteSegment(bytes)`.

`Types::WriteSegment` stores non-owning views. The caller-owned text and byte storage must remain alive until the write call returns.

## SegmentWriteOptions

`Types::SegmentWriteOptions` controls the batch:

- `styleMode` for styled segments;
- `appendLineEnding` after all segments;
- `lineEnding`;
- `flushMode`.

Per-segment style lives in each `WriteSegment`. The batch options do not apply one global style to all text.

## Performance contract

A segmented write should:

- serialize the target stream once for the whole batch;
- minimize backend write calls where practical;
- avoid requiring string concatenation;
- avoid unnecessary allocation on successful hot paths;
- reset style before returning when style was emitted.

Segmented writes do not guarantee atomicity relative to `std::cout`, `std::cerr`, `printf`, direct OS writes, or third-party terminal writes.
