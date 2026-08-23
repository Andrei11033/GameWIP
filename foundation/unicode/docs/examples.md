@page unicode_examples Examples

These examples use only the supported public header. Application-specific buffering, replacement, editing, rendering, and platform policy remain with
the caller.

## Classify an incremental UTF-8 prefix

```cpp
#include "unicode/unicode.h"

#include <string_view>

enum class PrefixState
{
    ScalarReady,
    NeedMoreInput,
    Reject,
};

PrefixState classifyPrefix(std::string_view bytes) noexcept
{
    const auto decoded = GameWIP::Unicode::Utf8::decodeScalar(bytes);
    switch (decoded.outcome)
    {
    case GameWIP::Unicode::Types::DecodeOutcome::Decoded:
        return PrefixState::ScalarReady;
    case GameWIP::Unicode::Types::DecodeOutcome::Incomplete:
        return PrefixState::NeedMoreInput;
    case GameWIP::Unicode::Types::DecodeOutcome::InvalidEncoding:
        return PrefixState::Reject;
    }

    return PrefixState::Reject;
}
```

Unicode classifies the encoded prefix. It does not decide whether the stream buffers more bytes, inserts `U+FFFD`, drops input, logs a diagnostic, or
closes a connection.

## Validate complete UTF-8

```cpp
#include "unicode/unicode.h"

#include <string_view>

bool isStrictUtf8(std::string_view text) noexcept
{
    return GameWIP::Unicode::Utf8::validate(text).outcome ==
           GameWIP::Unicode::Types::ValidationOutcome::Valid;
}
```

When validation fails, inspect the returned valid-prefix length before deciding whether partial progress is useful to the owning operation.

## Convert UTF-8 to UTF-16

Measurement lets the caller allocate exactly once; the Unicode conversion itself does not allocate:

```cpp
#include "unicode/unicode.h"

#include <optional>
#include <string_view>
#include <vector>

std::optional<std::vector<char16_t>> toUtf16(std::string_view source)
{
    namespace Unicode = GameWIP::Unicode;

    const Unicode::Types::Utf8::ToUtf16MeasureResult measured =
        Unicode::Utf8::measureToUtf16(source);
    if (measured.outcome != Unicode::Types::MeasureOutcome::Measured)
    {
        return std::nullopt;
    }

    std::vector<char16_t> result(measured.requiredCodeUnits);
    const Unicode::Types::Utf8::ToUtf16Result converted =
        Unicode::Utf8::convertToUtf16(source, result);
    if (converted.outcome != Unicode::Types::ConversionOutcome::Converted)
    {
        return std::nullopt;
    }

    return result;
}
```

No terminating `U+0000` code unit is appended. If a platform or C API requires a terminator, that boundary owns the additional storage and terminator
policy.

## Count extended grapheme clusters

```cpp
#include "unicode/unicode.h"

#include <optional>
#include <string_view>

std::optional<std::size_t> graphemeCount(std::string_view text) noexcept
{
    namespace Unicode = GameWIP::Unicode;

    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const Unicode::Types::Utf8::BoundaryResult next =
            Unicode::Utf8::nextGraphemeBoundary(text, offset);
        if (next.outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
        {
            return std::nullopt;
        }

        offset = next.byteOffset;
        ++count;
    }

    return count;
}
```

This counts Unicode extended grapheme clusters. It does not count glyphs, words, display columns, or terminal cells.

For a repeated walk, build one caller-backed index rather than making a stateless query at every step:

```cpp
#include "unicode/unicode.h"

#include <string>
#include <string_view>
#include <vector>

bool eraseSuffixByGrapheme(std::string &text)
{
    namespace Unicode = GameWIP::Unicode;

    std::vector<std::size_t> boundaries(text.size() + 1);
    Unicode::Utf8::GraphemeCursor cursor;
    if (cursor.reset(text, boundaries).outcome != Unicode::Types::Utf8::GraphemeIndexOutcome::Indexed ||
        cursor.seek(text.size()).outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
    {
        return false;
    }

    while (!text.empty())
    {
        const Unicode::Types::Utf8::BoundaryResult previous = cursor.previous();
        if (previous.outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
        {
            return false;
        }

        text.resize(previous.byteOffset);
        cursor.discardAfterCurrent();
    }

    return true;
}
```

The cursor does not retain the text. A suffix truncation exactly at its current indexed boundary can therefore discard later stale offsets in O(1).
Arbitrary insertion, replacement, or non-suffix deletion can change grapheme boundaries and requires re-indexing.

## Traverse backward

```cpp
#include "unicode/unicode.h"

#include <optional>
#include <string_view>

std::optional<std::size_t> previousClusterStart(
    std::string_view text,
    std::size_t offset) noexcept
{
    namespace Unicode = GameWIP::Unicode;

    const Unicode::Types::Utf8::BoundaryResult previous =
        Unicode::Utf8::previousGraphemeBoundary(text, offset);

    if (previous.outcome == Unicode::Types::Utf8::BoundaryOutcome::AtBeginning)
    {
        return std::size_t{0};
    }
    if (previous.outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
    {
        return std::nullopt;
    }

    return previous.byteOffset;
}
```

The offset may be `text.size()` or any code-point boundary inside a cluster. This stateless form is intended for isolated queries; use
`GraphemeCursor` for repeated stepping through the same segmentation.

## Query from inside a cluster

For UTF-8 text containing `a`, `U+0308 COMBINING DIAERESIS`, and `b`, byte offset 1 begins the combining mark. It is a code-point boundary but not a
grapheme boundary:

```cpp
#include "unicode/unicode.h"

#include <string_view>

bool verifyContainingCluster() noexcept
{
    constexpr std::string_view text = "a\xCC\x88"
                                      "b";

    const auto next = GameWIP::Unicode::Utf8::nextGraphemeBoundary(text, 1);
    const auto previous =
        GameWIP::Unicode::Utf8::previousGraphemeBoundary(text, 1);

    return next.outcome == GameWIP::Unicode::Types::Utf8::BoundaryOutcome::Found &&
           next.byteOffset == 3 &&
           previous.outcome == GameWIP::Unicode::Types::Utf8::BoundaryOutcome::Found &&
           previous.byteOffset == 0;
}
```

Callers do not need to precompute grapheme boundaries before an isolated query at a known code-point-aligned caret or selection offset. Repeated
forward/backward movement should use `GraphemeCursor`.
