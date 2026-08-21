#pragma once

/// @file type_aliases.h
/// @brief Source-tree-only migration aliases for Unicode implementation and validation code.

#include "unicode/unicode.h"

// Internal migration aliases keep implementation and validation code readable while the
// installed public surface uses the organized Types::Utf8/Utf16 vocabulary.
namespace GameWIP::Unicode::Types
{
    using BoundaryOutcome = Utf8::BoundaryOutcome;
    using GraphemeIndexOutcome = Utf8::GraphemeIndexOutcome;

    using Utf8DecodeResult = Utf8::DecodeResult;
    using Utf8EncodeResult = Utf8::EncodeResult;
    using Utf8ValidationResult = Utf8::ValidationResult;
    using Utf8ToUtf16MeasureResult = Utf8::ToUtf16MeasureResult;
    using Utf8ToUtf16Result = Utf8::ToUtf16Result;
    using Utf8BoundaryResult = Utf8::BoundaryResult;
    using Utf8GraphemeIndexResult = Utf8::GraphemeIndexResult;

    using Utf16DecodeResult = Utf16::DecodeResult;
    using Utf16EncodeResult = Utf16::EncodeResult;
    using Utf16ValidationResult = Utf16::ValidationResult;
    using Utf16ToUtf8MeasureResult = Utf16::ToUtf8MeasureResult;
    using Utf16ToUtf8Result = Utf16::ToUtf8Result;

    using UnicodeVersion = Version;
} // namespace GameWIP::Unicode::Types
