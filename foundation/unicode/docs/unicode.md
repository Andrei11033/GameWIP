@page unicode Unicode

`GameWIP::Unicode` is the platform-neutral Unicode text-processing library shared by low-level GameWIP components.

It provides strict UTF-8 and UTF-16 scalar operations, validation and conversion, UTF-8 code-point traversal, Unicode 17.0.0 default extended grapheme-cluster traversal, and caller-backed indexed grapheme traversal for repeated movement. GameWIP uses UTF-8 as its canonical public text representation; UTF-16 exists as an explicit bridge for boundaries that require it.

## Consumer manual

- @subpage unicode_quick_start
- @subpage unicode_public_api
- @subpage unicode_examples
- @subpage unicode_troubleshooting

## Maintainer validation

- @subpage unicode_testing

## Generated API reference

Use @ref GameWIP::Unicode for scalar and version helpers, @ref GameWIP::Unicode::Utf8 for UTF-8 and text-boundary operations, @ref GameWIP::Unicode::Utf16 for UTF-16 operations, and @ref GameWIP::Unicode::Types for passive outcomes and result values.

The generated reference documents every public declaration from `unicode/unicode.h`. The manual explains how those declarations compose, which failure and progress guarantees callers can rely on, and which policies intentionally remain outside the library.

## Key behavior

- Unicode scalar values are `U+0000` through `U+10FFFF`, excluding the surrogate range `U+D800` through `U+DFFF`.
- UTF-8 and UTF-16 decoding are strict. Complete input, valid incomplete prefixes, and malformed encodings have distinct deterministic outcomes.
- Empty ranges and embedded `U+0000` values are valid input.
- Public operations are `noexcept` and perform no implementation-owned dynamic allocation.
- Conversion writes only complete encoded scalars, appends no terminator, preserves completed progress, and leaves destination elements after the reported written extent untouched.
- Overlapping conversion source and destination ranges are rejected before output is written.
- UTF-8 grapheme traversal implements Unicode 17.0.0 default extended grapheme-cluster rules using checked-in generated property data.
- Stateless grapheme queries restart from the nearest provably safe local boundary when possible instead of always reconstructing context from byte 0.
- `Utf8::GraphemeCursor` indexes into caller-owned storage once, then supports constant-time forward/backward stepping and constant-time suffix-index discard without implementation-owned allocation.
- Independent calls use immutable data and no mutable process-wide or thread-local last-error state.
- Unicode does not define replacement or recovery policy, normalization, case conversion, collation, editing, rendering, locale behavior, or terminal-cell width.

## Dependency boundary

Unicode is a dependency-free static library installed as `GameWIP::Unicode`. The installed consumer entry point is `unicode/unicode.h`; generated property tables, internal codec helpers, and the generator are source-tree implementation details.

Unicode owns only reusable text algorithms. Platform API calls, filesystem path semantics, terminal sessions and editing, cursor/redraw behavior, status translation, and terminal-cell-width policy remain with their owning components.
