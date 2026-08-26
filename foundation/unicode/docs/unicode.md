@page unicode Unicode

`GameWIP::Unicode` is the platform-neutral Unicode text-processing library shared by low-level GameWIP components.

It provides strict UTF-8 and UTF-16 scalar operations, validation and conversion, UTF-8 code-point traversal, Unicode 17.0.0 default extended
grapheme-cluster traversal, and caller-backed indexed grapheme traversal for repeated movement. GameWIP uses UTF-8 as its canonical public text
representation; UTF-16 exists as an explicit bridge for boundaries that require it.

## How the library is organized

Unicode separates encoding from text boundaries. `Utf8` and `Utf16` validate,
decode, encode, and convert Unicode scalar values. The grapheme APIs operate on
already valid UTF-8 when code points must be grouped into user-perceived text
elements. Result values report both the outcome and completed progress, so a
caller can distinguish malformed input from a valid truncated prefix without
guessing how much data was consumed.

## Consumer manual

- @subpage unicode_quick_start — Include, link, validate, convert, and traverse
  text in a minimal program.
- @subpage unicode_public_api — Find each type and operation by capability and
  understand the shared result model.
- @subpage unicode_examples — See validation, conversion, scalar iteration, and
  grapheme traversal in context.
- @subpage unicode_troubleshooting — Diagnose malformed input, insufficient
  output space, overlap, and grapheme-boundary surprises.

## Maintainer validation

- @subpage unicode_testing — Understand conformance data, automated coverage,
  generator verification, and performance checks.

## Generated API reference

Use @ref GameWIP::Unicode for scalar and version helpers, @ref GameWIP::Unicode::Utf8 for UTF-8 and text-boundary operations, @ref
GameWIP::Unicode::Utf16 for UTF-16 operations, and @ref GameWIP::Unicode::Types for shared outcomes plus the organized `Types::Utf8` and
`Types::Utf16` result families.

The generated reference documents every public declaration from `unicode/unicode.h`. The manual explains how those declarations compose, which failure
and progress guarantees callers can rely on, and which policies intentionally remain outside the library.

## Key behavior

- Unicode scalar values are `U+0000` through `U+10FFFF`, excluding the surrogate range `U+D800` through `U+DFFF`.
- UTF-8 and UTF-16 decoding are strict. Complete input, valid incomplete prefixes, and malformed encodings have distinct deterministic outcomes.
- Empty ranges and embedded `U+0000` values are valid input.
- Public operations are `noexcept` and perform no implementation-owned dynamic allocation.
- Conversion writes only complete encoded scalars, appends no terminator, preserves completed progress, and leaves destination elements after the
  reported written extent untouched.
- Overlapping conversion source and destination ranges are rejected before output is written.
- UTF-8 grapheme traversal implements Unicode 17.0.0 default extended grapheme-cluster rules using checked-in generated property data.
- Stateless grapheme queries restart from the nearest provably safe local boundary when possible instead of always reconstructing context from byte 0.
- `Utf8::GraphemeCursor` indexes into caller-owned storage once, then supports constant-time forward/backward stepping and constant-time suffix-index
  discard without implementation-owned allocation.
- Independent calls use immutable data and no mutable process-wide or thread-local last-error state.
- Unicode does not define replacement or recovery policy, normalization, case conversion, collation, editing, rendering, locale behavior, or
  terminal-cell width.

## Dependency boundary

Unicode is installed as the static `GameWIP::Unicode` library with no reusable/public GameWIP library dependency. Its implementation may consume
narrow source-tree-only `GameWIP::Base` mechanisms such as checked arithmetic; Base is not exposed by the installed package. The installed consumer
entry point is `unicode/unicode.h`; generated property tables, internal codec helpers, and the generator are source-tree implementation details.

Unicode owns only reusable text algorithms. Platform API calls, filesystem path semantics, terminal sessions and editing, cursor/redraw behavior,
status translation, and terminal-cell-width policy remain with their owning components.
