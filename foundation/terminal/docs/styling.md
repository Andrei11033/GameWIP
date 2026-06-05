@page terminal_styling Terminal styling

Styling is emitted for streams whose capabilities report support. On unsupported streams, `StyleMode::Auto` writes plain text and `StyleMode::Always` reports `IO::Types::ErrorCode::Unsupported`.

## Text style

`Types::TextStyle` carries a portable style request:

- foreground color;
- background color;
- bold;
- dim;
- italic;
- underline;
- inverse;
- strikethrough.

Colors use `Types::Color`, `Types::ColorKind`, and `Types::BasicColor`. Use `defaultColor`, `basicColor`, and `rgbColor` to create color requests.

## TextWriteOptions

`Types::TextWriteOptions` contains style for one text write:

- `styleMode`;
- `style`;
- `flushMode`.

`Types::LineWriteOptions` contains the same style and flush fields plus `lineEnding` for `writeLine()` and `println()`.

Styling for one text or line call belongs in the operation options, not in a separate styled-write public function.

## StyleMode

`StyleMode::Never` writes plain text and should avoid style overhead.

`StyleMode::Auto` prepares the output stream when styling is not already active. If preparation or style support is unavailable, the call writes plain text.

`StyleMode::Always` prepares the stream when needed. A preparation failure or unsupported style is returned without writing.

## Style reset

Terminal assembles the style prefix, text, reset sequence, and optional line ending before one platform write. This prevents a failed middle write from leaving the stream in a partially styled state.

## Capability reporting

`Types::StyleCapabilities` reports portable style features:

- basic colors;
- RGB colors;
- bold;
- dim;
- italic;
- underline;
- inverse;
- strikethrough.

The public capability shape intentionally does not expose an ANSI/VT implementation flag. Callers should ask Terminal to emit styles instead of branching on the backend protocol.

## Segments

For per-segment styling, use `Types::WriteSegment` with `WriteSegmentKind::StyledText` and write the batch through `writeSegments(std::span<const Types::WriteSegment>, SegmentWriteOptions)`.

See @ref terminal_segmented_writes.
