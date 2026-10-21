/// @file utf16.cpp
/// @brief Strict UTF-16 scalar encoding, decoding, and validation.

#include "unicode/unicode.h"
#include "unicode/internal/encoding.h"

#include <cstddef>
#include <span>

namespace GameWIP::Unicode::Utf16
{
    Types::Utf16::DecodeResult decodeScalar(std::span<const char16_t> codeUnits) noexcept
    {
        return Internal::decodeUtf16Scalar(codeUnits);
    }

    Types::Utf16::EncodeResult encodeScalar(char32_t scalar) noexcept
    {
        if (!isScalarValue(scalar))
        {
            return {};
        }

        Types::Utf16::EncodeResult result{
            .codeUnitCount = Internal::utf16EncodedLength(scalar),
            .outcome = Types::EncodeOutcome::Encoded,
        };
        Internal::encodeUtf16Unchecked(scalar, result.codeUnits);
        return result;
    }

    Types::Utf16::ValidationResult validate(std::span<const char16_t> text) noexcept
    {
        std::size_t codeUnitOffset = 0;

        while (codeUnitOffset < text.size())
        {
            const Types::Utf16::DecodeResult decoded = Internal::decodeUtf16Scalar(text.subspan(codeUnitOffset));

            if (decoded.outcome == Types::DecodeOutcome::Incomplete)
            {
                return {
                    .validPrefixCodeUnits = codeUnitOffset,
                    .outcome = Types::ValidationOutcome::Incomplete,
                };
            }

            if (decoded.outcome == Types::DecodeOutcome::InvalidEncoding)
            {
                return {
                    .validPrefixCodeUnits = codeUnitOffset,
                    .outcome = Types::ValidationOutcome::InvalidEncoding,
                };
            }

            codeUnitOffset += decoded.codeUnitsConsumed;
        }

        return {
            .validPrefixCodeUnits = codeUnitOffset,
            .outcome = Types::ValidationOutcome::Valid,
        };
    }
} // namespace GameWIP::Unicode::Utf16
