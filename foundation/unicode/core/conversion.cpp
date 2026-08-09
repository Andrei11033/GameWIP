/// @file conversion.cpp
/// @brief Non-allocating strict UTF-8 and UTF-16 measurement and conversion.

#include "unicode/unicode.h"
#include "unicode/internal/encoding.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <string_view>

namespace GameWIP::Unicode
{
    namespace
    {
        /// @brief Returns whether two non-owning byte ranges overlap in the process address order.
        bool memoryRangesOverlap(const void *firstData, std::size_t firstSizeBytes, const void *secondData, std::size_t secondSizeBytes) noexcept
        {
            if (firstSizeBytes == 0 || secondSizeBytes == 0)
            {
                return false;
            }

            const auto *firstBegin = static_cast<const std::byte *>(firstData);
            const auto *secondBegin = static_cast<const std::byte *>(secondData);
            const auto *firstEnd = firstBegin + firstSizeBytes;
            const auto *secondEnd = secondBegin + secondSizeBytes;
            const std::less<const void *> less{};

            return less(firstBegin, secondEnd) && less(secondBegin, firstEnd);
        }

        /// @brief Maps a scalar-decoding failure to its measurement outcome.
        Types::MeasureOutcome measureOutcome(Types::DecodeOutcome outcome) noexcept
        {
            return outcome == Types::DecodeOutcome::Incomplete ? Types::MeasureOutcome::Incomplete : Types::MeasureOutcome::InvalidEncoding;
        }

        /// @brief Maps a scalar-decoding failure to its conversion outcome.
        Types::ConversionOutcome conversionOutcome(Types::DecodeOutcome outcome) noexcept
        {
            return outcome == Types::DecodeOutcome::Incomplete ? Types::ConversionOutcome::Incomplete : Types::ConversionOutcome::InvalidEncoding;
        }
    } // namespace

    namespace Utf8
    {
        Types::Utf8ToUtf16MeasureResult measureToUtf16(std::string_view source) noexcept
        {
            std::size_t sourceBytesProcessed = 0;
            std::size_t requiredCodeUnits = 0;

            while (sourceBytesProcessed < source.size())
            {
                const Types::Utf8DecodeResult decoded = Internal::decodeUtf8Scalar(source.substr(sourceBytesProcessed));
                if (decoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {
                        .sourceBytesProcessed = sourceBytesProcessed,
                        .requiredCodeUnits = requiredCodeUnits,
                        .outcome = measureOutcome(decoded.outcome),
                    };
                }

                const std::size_t scalarCodeUnits = Internal::utf16EncodedLength(decoded.scalar);
                if (scalarCodeUnits > std::numeric_limits<std::size_t>::max() - requiredCodeUnits)
                {
                    return {
                        .sourceBytesProcessed = sourceBytesProcessed,
                        .requiredCodeUnits = requiredCodeUnits,
                        .outcome = Types::MeasureOutcome::SizeLimitExceeded,
                    };
                }

                sourceBytesProcessed += decoded.bytesConsumed;
                requiredCodeUnits += scalarCodeUnits;
            }

            return {
                .sourceBytesProcessed = sourceBytesProcessed,
                .requiredCodeUnits = requiredCodeUnits,
                .outcome = Types::MeasureOutcome::Measured,
            };
        }

        Types::Utf8ToUtf16Result convertToUtf16(std::string_view source, std::span<char16_t> destination) noexcept
        {
            if (memoryRangesOverlap(source.data(), source.size(), destination.data(), destination.size_bytes()))
            {
                return {.outcome = Types::ConversionOutcome::OverlappingRanges};
            }

            std::size_t sourceBytesConsumed = 0;
            std::size_t codeUnitsWritten = 0;

            while (sourceBytesConsumed < source.size())
            {
                const Types::Utf8DecodeResult decoded = Internal::decodeUtf8Scalar(source.substr(sourceBytesConsumed));
                if (decoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {
                        .sourceBytesConsumed = sourceBytesConsumed,
                        .codeUnitsWritten = codeUnitsWritten,
                        .outcome = conversionOutcome(decoded.outcome),
                    };
                }

                const std::size_t scalarCodeUnits = Internal::utf16EncodedLength(decoded.scalar);
                if (scalarCodeUnits > destination.size() - codeUnitsWritten)
                {
                    return {
                        .sourceBytesConsumed = sourceBytesConsumed,
                        .codeUnitsWritten = codeUnitsWritten,
                        .outcome = Types::ConversionOutcome::DestinationTooSmall,
                    };
                }

                Internal::encodeUtf16Unchecked(decoded.scalar, destination.data() + codeUnitsWritten);
                sourceBytesConsumed += decoded.bytesConsumed;
                codeUnitsWritten += scalarCodeUnits;
            }

            return {
                .sourceBytesConsumed = sourceBytesConsumed,
                .codeUnitsWritten = codeUnitsWritten,
                .outcome = Types::ConversionOutcome::Converted,
            };
        }
    } // namespace Utf8

    namespace Utf16
    {
        Types::Utf16ToUtf8MeasureResult measureToUtf8(std::span<const char16_t> source) noexcept
        {
            std::size_t sourceCodeUnitsProcessed = 0;
            std::size_t requiredBytes = 0;

            while (sourceCodeUnitsProcessed < source.size())
            {
                const Types::Utf16DecodeResult decoded = Internal::decodeUtf16Scalar(source.subspan(sourceCodeUnitsProcessed));
                if (decoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {
                        .sourceCodeUnitsProcessed = sourceCodeUnitsProcessed,
                        .requiredBytes = requiredBytes,
                        .outcome = measureOutcome(decoded.outcome),
                    };
                }

                const std::size_t scalarBytes = Internal::utf8EncodedLength(decoded.scalar);
                if (scalarBytes > std::numeric_limits<std::size_t>::max() - requiredBytes)
                {
                    return {
                        .sourceCodeUnitsProcessed = sourceCodeUnitsProcessed,
                        .requiredBytes = requiredBytes,
                        .outcome = Types::MeasureOutcome::SizeLimitExceeded,
                    };
                }

                sourceCodeUnitsProcessed += decoded.codeUnitsConsumed;
                requiredBytes += scalarBytes;
            }

            return {
                .sourceCodeUnitsProcessed = sourceCodeUnitsProcessed,
                .requiredBytes = requiredBytes,
                .outcome = Types::MeasureOutcome::Measured,
            };
        }

        Types::Utf16ToUtf8Result convertToUtf8(std::span<const char16_t> source, std::span<char> destination) noexcept
        {
            if (memoryRangesOverlap(source.data(), source.size_bytes(), destination.data(), destination.size()))
            {
                return {.outcome = Types::ConversionOutcome::OverlappingRanges};
            }

            std::size_t sourceCodeUnitsConsumed = 0;
            std::size_t bytesWritten = 0;

            while (sourceCodeUnitsConsumed < source.size())
            {
                const Types::Utf16DecodeResult decoded = Internal::decodeUtf16Scalar(source.subspan(sourceCodeUnitsConsumed));
                if (decoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {
                        .sourceCodeUnitsConsumed = sourceCodeUnitsConsumed,
                        .bytesWritten = bytesWritten,
                        .outcome = conversionOutcome(decoded.outcome),
                    };
                }

                const std::size_t scalarBytes = Internal::utf8EncodedLength(decoded.scalar);
                if (scalarBytes > destination.size() - bytesWritten)
                {
                    return {
                        .sourceCodeUnitsConsumed = sourceCodeUnitsConsumed,
                        .bytesWritten = bytesWritten,
                        .outcome = Types::ConversionOutcome::DestinationTooSmall,
                    };
                }

                Internal::encodeUtf8Unchecked(decoded.scalar, destination.data() + bytesWritten);
                sourceCodeUnitsConsumed += decoded.codeUnitsConsumed;
                bytesWritten += scalarBytes;
            }

            return {
                .sourceCodeUnitsConsumed = sourceCodeUnitsConsumed,
                .bytesWritten = bytesWritten,
                .outcome = Types::ConversionOutcome::Converted,
            };
        }
    } // namespace Utf16
} // namespace GameWIP::Unicode
