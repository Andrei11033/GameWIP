@page terminal_styling Styling

Terminal accepts portable style requests and emits them only when the selected endpoint reports support.

## Color and style values

Create colors with `defaultColor()`, `basicColor()`, or `rgbColor()`. `Types::Style::BasicColor` provides the normal and bright variants of black,
red, green, yellow, blue, magenta, cyan, and white.

`Types::Style::Request` contains foreground/background color plus bold, dim, italic, underline, inverse, and strikethrough requests. The corresponding
`Types::Style::Capabilities` fields describe what the current endpoint can honor.

## Per-operation options

`Types::Output::TextOptions` contains `styleMode`, `style`, and `flushMode`. `Types::Output::LineOptions` adds `lineEnding`. Per-segment style is
stored in each `Types::Output::Segment`; `Types::Output::SegmentOptions::styleMode` controls how styled segments handle unavailable features.

## Style modes

- `Never` emits plain text and skips style capability work.
- `Auto` prepares when useful and falls back to plain text when the requested style cannot be honored.
- `Required` returns preparation or `Unsupported` failure without normal text emission when the complete requested style is unavailable.

Terminal treats a style request as a whole. It does not silently emit only a supported subset of attributes.

## Emission and reset

Terminal assembles the style prefix, text, reset sequence, and optional line ending into one logical operation where practical. This reduces
interleaving between those pieces, but output is not transactional: a platform write can complete partially and leave a prefix without its reset
sequence.

Use `resetStyle()` to request an explicit portable reset when recovering from output not controlled entirely by Terminal. It still requires a
supported terminal endpoint.

## Redirected output

In `Auto`, redirected streams normally receive plain text without styling bytes. `Required` succeeds only if the endpoint can honestly support the
request. Do not branch on an assumed ANSI/VT flag; query portable capabilities or let Terminal apply the selected style mode.

## Selection guidance

Use plain `writeText()`/`writeLine()` with default styling when styles are unnecessary. For mixed styles in one logical record, use @ref
terminal_segmented_writes. Allocation and exception behavior is owned by @ref terminal_read_write.
