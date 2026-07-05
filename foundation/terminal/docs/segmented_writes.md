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
- `styledTextSegment(text, style)`;
- `byteSegment(bytes)`.

`Types::WriteSegment` stores non-owning views. The caller-owned text and byte storage must remain alive until the write call returns.

`WriteSegment` is valid by construction: its kind and payload cannot be changed independently after a factory creates it. `Color` follows the same rule and exposes read-only accessors.

## SegmentWriteOptions

`Types::SegmentWriteOptions` controls the batch:

- `styleMode` for styled segments;
- `appendLineEnding` after all segments;
- `lineEnding`;
- `flushMode`.

Per-segment style lives in each `WriteSegment`. The batch options do not apply one global style to all text.

Terminal validates the complete batch before emitting output. A byte segment targeting a real Win32 console returns `Unsupported` without writing earlier segments. Redirected batches may contain byte segments and are emitted as one Terminal operation.

Text-only and styled batches are emitted as one Terminal operation. Unusually large batches do not permanently retain their peak temporary capacity.

Plain text-only batches skip capability queries. Capability work is performed only when byte segments or non-default styled segments require it.

Segmented writes do not guarantee atomicity relative to `std::cout`, `std::cerr`, `printf`, direct operating-system writes, or third-party terminal writes.
