/// @file utf8.cpp
/// @brief Strict UTF-8 scalar encoding, decoding, validation, and code-point traversal.

#include "unicode/unicode.h"
#include "unicode/internal/encoding.h"

#include <cstddef>
#include <string_view>

namespace GameWIP::Unicode::Utf8
{
    Types::Utf8DecodeResult decodeScalar(std::string_view bytes) noexcept
    {
        return Internal::decodeUtf8Scalar(bytes);
    }

    Types::Utf8EncodeResult encodeScalar(char32_t scalar) noexcept
    {
        if (!isScalarValue(scalar))
        {
            return {};
        }

        Types::Utf8EncodeResult result{
            .byteCount = Internal::utf8EncodedLength(scalar),
            .outcome = Types::EncodeOutcome::Encoded,
        };
        Internal::encodeUtf8Unchecked(scalar, result.bytes);
        return result;
    }

    Types::Utf8ValidationResult validate(std::string_view text) noexcept
    {
        std::size_t byteOffset = 0;

        while (byteOffset < text.size())
        {
            const Types::Utf8DecodeResult decoded = Internal::decodeUtf8Scalar(text.substr(byteOffset));
            if (decoded.outcome == Types::DecodeOutcome::Incomplete)
            {
                return {
                    .validPrefixBytes = byteOffset,
                    .outcome = Types::ValidationOutcome::Incomplete,
                };
            }
            if (decoded.outcome == Types::DecodeOutcome::InvalidEncoding)
            {
                return {
                    .validPrefixBytes = byteOffset,
                    .outcome = Types::ValidationOutcome::InvalidEncoding,
                };
            }

            byteOffset += decoded.bytesConsumed;
        }

        return {.validPrefixBytes = byteOffset, .outcome = Types::ValidationOutcome::Valid};
    }

    Types::Utf8BoundaryResult nextCodePointBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        if (byteOffset > text.size())
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (byteOffset < text.size() && Internal::isContinuationByte(text[byteOffset]))
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }

        if (byteOffset > 0)
        {
            const Internal::PreviousUtf8ScalarResult previous = Internal::decodePreviousUtf8Scalar(text, byteOffset);
            if (previous.decoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
            }
        }

        if (byteOffset == text.size())
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::AtEnd};
        }

        const Types::Utf8DecodeResult decoded = Internal::decodeUtf8Scalar(text.substr(byteOffset));
        if (decoded.outcome != Types::DecodeOutcome::Decoded)
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
        }

        return {
            .byteOffset = byteOffset + decoded.bytesConsumed,
            .outcome = Types::BoundaryOutcome::Found,
        };
    }

    Types::Utf8BoundaryResult previousCodePointBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        if (byteOffset > text.size())
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (byteOffset < text.size() && Internal::isContinuationByte(text[byteOffset]))
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (byteOffset == 0)
        {
            return {.byteOffset = 0, .outcome = Types::BoundaryOutcome::AtBeginning};
        }

        const Internal::PreviousUtf8ScalarResult previous = Internal::decodePreviousUtf8Scalar(text, byteOffset);
        if (previous.decoded.outcome != Types::DecodeOutcome::Decoded)
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
        }

        return {.byteOffset = previous.startOffset, .outcome = Types::BoundaryOutcome::Found};
    }
} // namespace GameWIP::Unicode::Utf8
