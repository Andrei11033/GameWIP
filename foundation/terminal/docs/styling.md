@page foundation_terminal_styling Terminal styling

This page documents Terminal text styling behavior.

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

`StyleMode::Auto` emits style only when the target stream supports the requested style. When styling is not supported, the call writes plain text.

`StyleMode::Always` attempts to force styling. If the backend cannot support the requested style on that stream, the call reports `IO::Types::ErrorCode::Unsupported`.

## Style reset

When Terminal emits style for a text write, it must reset style before returning.

If text output succeeds but resetting style fails, the operation returns the reset failure. The diagnostic status should make clear that the text was already written and the reset failed afterward.

If text output fails after style was emitted, the implementation should still attempt a best-effort reset before reporting the write failure.

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

See @ref foundation_terminal_segmented_writes.
