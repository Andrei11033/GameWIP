@page terminal_segmented_writes Terminal segmented writes

Segmented writes let callers write a mixed batch of text, styled text, and bytes without concatenating strings first.

The optimized path is:

```cpp
GameWIP::Terminal::writeSegments(std::span<const GameWIP::Terminal::Types::WriteSegment>{segments});
```

Use segmented writes when one logical output record contains independently styled or byte-oriented parts.

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

Terminal validates the complete batch before emitting output. A byte segment targeting a real Win32 console returns `Unsupported` without writing earlier segments. Redirected batches may contain byte segments and are assembled into one backend write.

Text-only and styled batches are assembled into one backend text write. Per-stream scratch capacity is reused for normal writes and released after unusually large batches.

Segmented writes do not guarantee atomicity relative to `std::cout`, `std::cerr`, `printf`, direct OS writes, or third-party terminal writes.
