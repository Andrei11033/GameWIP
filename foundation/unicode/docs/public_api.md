@page unicode_public_api Public API

Include `unicode/unicode.h`. Installed consumers link `GameWIP::Unicode`; source-tree consumers link `Unicode`. See @ref unicode_quick_start for complete CMake usage.

## Constants

| API | Purpose |
| --- | --- |
| `kMaximumScalarValue` | Highest possible Unicode scalar value, `U+10FFFF`; surrogate code points remain invalid scalar values. |
| `Utf8::kMaximumScalarBytes` | Maximum strict UTF-8 encoding length for one scalar: 4 bytes. |
| `Utf16::kMaximumScalarCodeUnits` | Maximum UTF-16 encoding length for one scalar: 2 code units. |

## Outcome and result types

The `GameWIP::Unicode::Types` namespace contains shared outcomes and `Version`. Encoding-specific passive values live under `Types::Utf8` and `Types::Utf16`; the installed public header provides no flat compatibility aliases.

### Decode and encode outcomes

`DecodeOutcome` distinguishes:

- `Decoded`: one complete scalar was decoded.
- `Incomplete`: the supplied encoded input is a valid prefix that requires additional input.
- `InvalidEncoding`: the leading sequence is malformed and cannot become valid by appending input.

Decode failure returns scalar `U+0000` and consumes zero bytes or code units.

`EncodeOutcome` is `Encoded` for a valid scalar or `InvalidScalar` for a surrogate code point or a value above `U+10FFFF`. Invalid scalar encoding reports a zero encoded length.

### Validation outcome

`ValidationOutcome` is:

- `Valid` when the complete range is valid.
- `Incomplete` when the complete valid prefix is followed by a truncated final sequence.
- `InvalidEncoding` when the complete valid prefix is followed by malformed input.

`Types::Utf8::ValidationResult::validPrefixBytes` and `Types::Utf16::ValidationResult::validPrefixCodeUnits` count only complete valid input before the failing sequence.

### Measurement outcome

`MeasureOutcome` is:

- `Measured` when the complete source is valid and its destination requirement was measured.
- `Incomplete` or `InvalidEncoding` when the measured valid prefix is followed by the corresponding source failure.
- `SizeLimitExceeded` when the required destination count cannot be represented by `std::size_t`.

Measurement results retain both the processed source extent and the required destination size for the completed valid prefix.

### Conversion outcome

`ConversionOutcome` is:

- `Converted` when the complete source was converted.
- `Incomplete` or `InvalidEncoding` when completed conversion progress is followed by the corresponding source failure.
- `DestinationTooSmall` when the destination cannot hold the next complete encoded scalar.
- `OverlappingRanges` when source and destination memory overlap.

Overlap is rejected before writes. Destination exhaustion stops before the next scalar, surrogate pair, or UTF-8 sequence; no partial encoding is written.

### Boundary outcome

`Types::Utf8::BoundaryOutcome` is:

- `Found` when the requested next or previous boundary was found.
- `AtBeginning` when backward traversal starts at byte offset 0.
- `AtEnd` when forward traversal starts at `text.size()`.
- `InvalidOffset` when the offset is outside the range or is not UTF-8 code-point aligned.
- `InvalidEncoding` when malformed or incomplete UTF-8 prevents the requested traversal.

`Types::Utf8::BoundaryResult::byteOffset` is the discovered boundary on `Found`. Endpoint outcomes retain the endpoint, and failures retain the original caller-provided offset.

### Grapheme indexing outcome

`Types::Utf8::GraphemeIndexOutcome` is:

- `Indexed` when complete valid UTF-8 was segmented into caller-owned boundary storage.
- `DestinationTooSmall` when the supplied boundary span cannot hold the complete index.
- `InvalidEncoding` when malformed or incomplete UTF-8 prevents complete indexing.

`Types::Utf8::GraphemeIndexResult::requiredBoundaryCount` includes offset 0 and the final `text.size()` boundary. It is meaningful on `Indexed` and `DestinationTooSmall`; malformed input reports zero. Empty text therefore requires one boundary entry.

## Result structures

| Result | Fields |
| --- | --- |
| `Types::Utf8::DecodeResult` | `scalar`, `bytesConsumed`, `outcome` |
| `Types::Utf8::EncodeResult` | fixed `bytes`, `byteCount`, `outcome` |
| `Types::Utf8::ValidationResult` | `validPrefixBytes`, `outcome` |
| `Types::Utf8::ToUtf16MeasureResult` | `sourceBytesProcessed`, `requiredCodeUnits`, `outcome` |
| `Types::Utf8::ToUtf16Result` | `sourceBytesConsumed`, `codeUnitsWritten`, `outcome` |
| `Types::Utf8::BoundaryResult` | `byteOffset`, `outcome` |
| `Types::Utf8::GraphemeIndexResult` | `requiredBoundaryCount`, `outcome` |
| `Types::Utf16::DecodeResult` | `scalar`, `codeUnitsConsumed`, `outcome` |
| `Types::Utf16::EncodeResult` | fixed `codeUnits`, `codeUnitCount`, `outcome` |
| `Types::Utf16::ValidationResult` | `validPrefixCodeUnits`, `outcome` |
| `Types::Utf16::ToUtf8MeasureResult` | `sourceCodeUnitsProcessed`, `requiredBytes`, `outcome` |
| `Types::Utf16::ToUtf8Result` | `sourceCodeUnitsConsumed`, `bytesWritten`, `outcome` |
| `Types::Version` | `major`, `minor`, `patch` |

## Scalar and version helpers

`isScalarValue()` accepts values from `U+0000` through `U+10FFFF` except `U+D800` through `U+DFFF`.

`getStandardVersion()` reports the Unicode Standard version used by generated property data and grapheme segmentation. The current result is `17.0.0`.

## UTF-8 operations

### `decodeScalar()`

`Utf8::decodeScalar()` decodes only the leading scalar in the supplied view. Bytes after that scalar are not inspected. This makes the function suitable for incremental owners that retain their own stream state.

### `encodeScalar()`

`Utf8::encodeScalar()` encodes one valid scalar into the fixed four-byte result array and reports the valid prefix length through `byteCount`.

### `validate()`

`Utf8::validate()` validates the complete byte range and reports the complete valid prefix before a truncated or malformed sequence. Empty input and embedded `U+0000` are valid.

### `measureToUtf16()` and `convertToUtf16()`

Measurement computes the UTF-16 storage required by the complete valid source prefix.

Conversion writes directly to caller-provided storage. It appends no terminator, never writes a partial surrogate pair, leaves elements after `codeUnitsWritten` untouched, and preserves completed source/output progress when a later scalar fails or does not fit.

Source and destination memory must not overlap. Overlap is reported as `OverlappingRanges` with zero progress and no writes.

### Code-point boundaries

`nextCodePointBoundary()` and `previousCodePointBoundary()` require a code-point-aligned byte offset no greater than `text.size()`.

Forward traversal validates the scalar beginning at the offset and, when needed to establish alignment, the scalar ending at the offset. Backward traversal validates the scalar ending at the offset. Malformed or incomplete UTF-8 required by the operation is reported as `InvalidEncoding`.

### Grapheme boundaries

`nextGraphemeBoundary()` and `previousGraphemeBoundary()` implement Unicode 17.0.0 default extended grapheme-cluster traversal using the library's pinned generated properties.

The implementation is the Unicode default algorithm; it does not apply CLDR or locale-specific grapheme tailoring.

The supplied offset must be code-point aligned but does not need to be a grapheme boundary:

- From inside a cluster, `nextGraphemeBoundary()` returns the end of the containing cluster.
- From inside a cluster, `previousGraphemeBoundary()` returns the beginning of the containing cluster.
- Forward traversal at the end reports `AtEnd`.
- Backward traversal at the beginning reports `AtBeginning`.

Context-sensitive grapheme rules can require inspection of earlier text to reconstruct segmentation state. Malformed or incomplete UTF-8 encountered in the context required by the traversal is reported as `InvalidEncoding`.

For isolated non-ASCII queries, the stateless implementation searches backward for the nearest boundary whose break is provable without earlier grapheme state, then reconstructs state only from that restart point. State-sensitive sequences can still force a scan farther left, so worst-case random access remains O(n).

For repeated traversal, use `Utf8::GraphemeCursor`. `reset()` performs one complete segmentation pass and stores all boundary offsets in caller-provided `std::size_t` storage. After a successful reset, `next()` and `previous()` are O(1), `seek()` is O(log graphemes), and `discardAfterCurrent()` drops later retained offsets in O(1).

The cursor retains only the caller-owned boundary span, not the text. The storage must remain alive and unmodified while indexed. A caller may truncate a text suffix exactly at the current indexed boundary and then call `discardAfterCurrent()`; arbitrary insertion, replacement, normalization, or non-suffix deletion can change surrounding boundaries and requires re-indexing.

## UTF-16 operations

`isHighSurrogate()`, `isLowSurrogate()`, and `isSurrogate()` classify UTF-16 code units without allocation.

`Utf16::decodeScalar()` accepts one BMP non-surrogate or one valid high/low surrogate pair. A trailing high surrogate is `Incomplete`; an isolated low surrogate or a high surrogate followed by a non-low surrogate is `InvalidEncoding`.

`Utf16::encodeScalar()` writes one BMP code unit or one supplementary surrogate pair to its fixed result.

`Utf16::validate()` reports the complete valid code-unit prefix before malformed or incomplete input.

`measureToUtf8()` and `convertToUtf8()` mirror the UTF-8-to-UTF-16 contracts: prefix measurement, completed progress, no partial UTF-8 sequence, no terminator, untouched destination tail, and overlap rejection before writes.

## Ownership, threading, and performance

Views and spans are non-owning. Their storage must remain valid for the duration of the call, and callers must not create data races on mutable destination storage.

Public operations are `noexcept`, perform no implementation-owned dynamic allocation, perform no I/O, and use immutable generated tables rather than mutable global or thread-local state. Independent calls can therefore run concurrently when their caller-owned memory does not race.

UTF-8 scalar decoding and code-point traversal operate on bounded local input. Property lookup is constant-time. Stateless grapheme random access uses a nearest-safe-restart optimization but can still require O(n) lookbehind in state-sensitive cases. `Utf8::GraphemeCursor` makes one full O(n) index pass so a complete repeated forward/backward walk or suffix-deletion traversal remains O(n) overall.

## Package boundary

Unicode is installed as a dependency-free static `GameWIP::Unicode` target with the public header `unicode/unicode.h`.

The public boundary exposes C++ standard-library fixed arrays, spans, string views, integer types, and `char32_t`. Consumers therefore follow the project compiler, standard-library, runtime, and exact-version compatibility policy documented in @ref project_library_compatibility.

Headers under `unicode/internal`, the generated table representation, generator implementation, validation sources, and benchmark sources are not installed interfaces. The Unicode Standard version is a data/behavior version and is independent of the GameWIP CMake package version.

## Related pages

- @ref unicode_examples
- @ref unicode_testing
- @ref unicode_troubleshooting
- @ref project_library_compatibility
