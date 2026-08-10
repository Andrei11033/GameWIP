@page unicode_quick_start Quick start

## Include

```cpp
#include "unicode/unicode.h"
```

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Unicode ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Unicode)
```

Unicode has no transitive GameWIP package dependency.

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE Unicode)
```

## Minimal usage

Strict UTF-8 decoding distinguishes a complete scalar from a valid incomplete prefix and malformed input:

```cpp
#include "unicode/unicode.h"

#include <string_view>

int leadingScalarByteCount(std::string_view input) noexcept
{
    namespace Unicode = GameWIP::Unicode;

    const Unicode::Types::Utf8DecodeResult decoded = Unicode::Utf8::decodeScalar(input);
    if (decoded.outcome == Unicode::Types::DecodeOutcome::Decoded)
    {
        return static_cast<int>(decoded.bytesConsumed);
    }

    // Zero means that a streaming owner needs more input; -1 means malformed input.
    return decoded.outcome == Unicode::Types::DecodeOutcome::Incomplete ? 0 : -1;
}
```

On `Incomplete` or `InvalidEncoding`, decoding returns scalar `U+0000` and consumes zero bytes. The caller owns buffering, replacement, retry, logging, and rejection policy.

## Failure handling

Inspect the result outcome before consuming success-only fields:

- Decode failure consumes zero input and returns scalar `U+0000`.
- Invalid scalar encoding reports `InvalidScalar` and a zero encoded length.
- Validation reports the complete valid prefix before incomplete or malformed input.
- Measurement reports source and required-destination progress through the last complete valid scalar.
- Conversion may preserve completed progress before source failure or destination exhaustion.
- `DestinationTooSmall` never writes part of the next encoded scalar.
- `OverlappingRanges` is rejected before any output is written.
- Boundary failures preserve the caller-provided byte offset.
- Grapheme traversal reports malformed or incomplete UTF-8 as `InvalidEncoding`; it does not apply replacement-character recovery.
- Repeated grapheme traversal should use `Utf8::GraphemeCursor`, which stores boundary offsets in caller-provided storage and performs no implementation-owned allocation.
- A sizing `GraphemeCursor::reset()` may return `DestinationTooSmall` together with the complete `requiredBoundaryCount`; resize caller storage and retry.

All public operations are `noexcept` and perform no implementation-owned dynamic allocation.

## Where to go next

- @ref unicode_public_api inventories the complete public surface and result contracts.
- @ref unicode_examples shows conversion and boundary-traversal patterns.
- @ref unicode_testing documents Unicode data provenance, regeneration, conformance, and benchmarks.
- @ref unicode_troubleshooting maps common symptoms to the owning contract.
