@page unicode_quick_start Quick start

This example validates one UTF-8 string and shows the strict error model.

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

    const Unicode::Types::Utf8::DecodeResult decoded = Unicode::Utf8::decodeScalar(input);
    if (decoded.outcome == Unicode::Types::DecodeOutcome::Decoded)
    {
        return static_cast<int>(decoded.bytesConsumed);
    }

    // Zero means that a streaming owner needs more input; -1 means malformed input.
    return decoded.outcome == Unicode::Types::DecodeOutcome::Incomplete ? 0 : -1;
}
```

On `Incomplete` or `InvalidEncoding`, decoding returns scalar `U+0000` and consumes zero bytes. The caller owns buffering, replacement, retry,
logging, and rejection policy.

## Failure handling

Inspect the result outcome before consuming success-only fields. Failures preserve completed progress and never write a partial scalar; see @ref
unicode_public_api for the full result model and @ref unicode_troubleshooting for each outcome.

For repeated grapheme traversal, use `Utf8::GraphemeCursor` with caller-provided boundary storage. If `reset()` returns `DestinationTooSmall`, resize
to the reported `requiredBoundaryCount` and retry.

All public operations are `noexcept` and perform no implementation-owned dynamic allocation.

## Where to go next

- @ref unicode_public_api inventories the complete public surface and result contracts.
- @ref unicode_examples shows conversion and boundary-traversal patterns.
- @ref unicode_testing documents Unicode data provenance, regeneration, conformance, and benchmarks.
- @ref unicode_troubleshooting maps common symptoms to the owning contract.
