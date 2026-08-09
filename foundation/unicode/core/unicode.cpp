/// @file unicode.cpp
/// @brief Unicode Standard version reporting.

#include "unicode/unicode.h"
#include "unicode/internal/generated/unicode_properties.h"

namespace GameWIP::Unicode
{
    Types::UnicodeVersion getStandardVersion() noexcept
    {
        return {
            .major = Internal::Generated::kUnicodeVersionMajor,
            .minor = Internal::Generated::kUnicodeVersionMinor,
            .patch = Internal::Generated::kUnicodeVersionPatch,
        };
    }
} // namespace GameWIP::Unicode
