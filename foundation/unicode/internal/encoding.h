/// @file encoding.h
/// @brief Internal non-allocating Unicode scalar codec primitives.

#pragma once

#include "unicode/unicode.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace GameWIP::Unicode::Internal
{
    /// @brief Converts one potentially signed UTF-8 storage byte to its numeric value.
    [[nodiscard]] constexpr std::uint8_t byteValue(char value) noexcept
    {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    }

    /// @brief Returns whether one byte has the UTF-8 continuation-byte form.
    [[nodiscard]] constexpr bool isContinuationByte(char value) noexcept
    {
        return (byteValue(value) & 0xC0U) == 0x80U;
    }

    /// @brief Decodes one strict UTF-8 scalar without allocation or external state.
    [[nodiscard]] inline Types::Utf8DecodeResult decodeUtf8Scalar(std::string_view bytes) noexcept
    {
        if (bytes.empty())
        {
            return {};
        }

        const std::uint8_t first = byteValue(bytes[0]);

        if (first <= 0x7FU)
        {
            return {
                .scalar = static_cast<char32_t>(first),
                .bytesConsumed = 1,
                .outcome = Types::DecodeOutcome::Decoded,
            };
        }

        if (first < 0xC2U)
        {
            return {.outcome = Types::DecodeOutcome::InvalidEncoding};
        }

        if (first <= 0xDFU)
        {
            if (bytes.size() < 2)
            {
                return {};
            }

            const std::uint8_t second = byteValue(bytes[1]);
            if ((second & 0xC0U) != 0x80U)
            {
                return {.outcome = Types::DecodeOutcome::InvalidEncoding};
            }

            const char32_t scalar = static_cast<char32_t>(((first & 0x1FU) << 6U) | (second & 0x3FU));
            return {.scalar = scalar, .bytesConsumed = 2, .outcome = Types::DecodeOutcome::Decoded};
        }

        if (first <= 0xEFU)
        {
            if (bytes.size() < 2)
            {
                return {};
            }

            const std::uint8_t second = byteValue(bytes[1]);
            const bool secondIsValid = first == 0xE0U   ? second >= 0xA0U && second <= 0xBFU
                                       : first == 0xEDU ? second >= 0x80U && second <= 0x9FU
                                                        : (second & 0xC0U) == 0x80U;

            if (!secondIsValid)
            {
                return {.outcome = Types::DecodeOutcome::InvalidEncoding};
            }

            if (bytes.size() < 3)
            {
                return {};
            }

            const std::uint8_t third = byteValue(bytes[2]);
            if ((third & 0xC0U) != 0x80U)
            {
                return {.outcome = Types::DecodeOutcome::InvalidEncoding};
            }

            const char32_t scalar = static_cast<char32_t>(((first & 0x0FU) << 12U) | ((second & 0x3FU) << 6U) | (third & 0x3FU));
            return {.scalar = scalar, .bytesConsumed = 3, .outcome = Types::DecodeOutcome::Decoded};
        }

        if (first <= 0xF4U)
        {
            if (bytes.size() < 2)
            {
                return {};
            }

            const std::uint8_t second = byteValue(bytes[1]);
            const bool secondIsValid = first == 0xF0U   ? second >= 0x90U && second <= 0xBFU
                                       : first == 0xF4U ? second >= 0x80U && second <= 0x8FU
                                                        : (second & 0xC0U) == 0x80U;

            if (!secondIsValid)
            {
                return {.outcome = Types::DecodeOutcome::InvalidEncoding};
            }

            if (bytes.size() < 3)
            {
                return {};
            }

            const std::uint8_t third = byteValue(bytes[2]);
            if ((third & 0xC0U) != 0x80U)
            {
                return {.outcome = Types::DecodeOutcome::InvalidEncoding};
            }

            if (bytes.size() < 4)
            {
                return {};
            }

            const std::uint8_t fourth = byteValue(bytes[3]);
            if ((fourth & 0xC0U) != 0x80U)
            {
                return {.outcome = Types::DecodeOutcome::InvalidEncoding};
            }

            const char32_t scalar =
                static_cast<char32_t>(((first & 0x07U) << 18U) | ((second & 0x3FU) << 12U) | ((third & 0x3FU) << 6U) | (fourth & 0x3FU));
            return {.scalar = scalar, .bytesConsumed = 4, .outcome = Types::DecodeOutcome::Decoded};
        }

        return {.outcome = Types::DecodeOutcome::InvalidEncoding};
    }

    /// @brief Returns the UTF-8 byte count for a known-valid Unicode scalar.
    [[nodiscard]] constexpr std::uint8_t utf8EncodedLength(char32_t scalar) noexcept
    {
        if (scalar <= static_cast<char32_t>(0x7F))
        {
            return 1;
        }
        if (scalar <= static_cast<char32_t>(0x7FF))
        {
            return 2;
        }
        if (scalar <= static_cast<char32_t>(0xFFFF))
        {
            return 3;
        }
        return 4;
    }

    /// @brief Encodes one known-valid scalar into at least four writable UTF-8 bytes.
    inline void encodeUtf8Unchecked(char32_t scalar, char *destination) noexcept
    {
        const std::uint32_t value = static_cast<std::uint32_t>(scalar);

        if (value <= 0x7FU)
        {
            destination[0] = static_cast<char>(value);
            return;
        }

        if (value <= 0x7FFU)
        {
            destination[0] = static_cast<char>(0xC0U | (value >> 6U));
            destination[1] = static_cast<char>(0x80U | (value & 0x3FU));
            return;
        }

        if (value <= 0xFFFFU)
        {
            destination[0] = static_cast<char>(0xE0U | (value >> 12U));
            destination[1] = static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
            destination[2] = static_cast<char>(0x80U | (value & 0x3FU));
            return;
        }

        destination[0] = static_cast<char>(0xF0U | (value >> 18U));
        destination[1] = static_cast<char>(0x80U | ((value >> 12U) & 0x3FU));
        destination[2] = static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
        destination[3] = static_cast<char>(0x80U | (value & 0x3FU));
    }

    /// @brief Decodes one strict UTF-16 scalar without allocation or external state.
    [[nodiscard]] inline Types::Utf16DecodeResult decodeUtf16Scalar(std::span<const char16_t> codeUnits) noexcept
    {
        if (codeUnits.empty())
        {
            return {};
        }

        const char16_t first = codeUnits[0];
        if (!Utf16::isSurrogate(first))
        {
            return {
                .scalar = static_cast<char32_t>(first),
                .codeUnitsConsumed = 1,
                .outcome = Types::DecodeOutcome::Decoded,
            };
        }

        if (Utf16::isLowSurrogate(first))
        {
            return {.outcome = Types::DecodeOutcome::InvalidEncoding};
        }

        if (codeUnits.size() < 2)
        {
            return {};
        }

        const char16_t second = codeUnits[1];
        if (!Utf16::isLowSurrogate(second))
        {
            return {.outcome = Types::DecodeOutcome::InvalidEncoding};
        }

        constexpr std::uint32_t highBase = 0xD800U;
        constexpr std::uint32_t lowBase = 0xDC00U;
        constexpr std::uint32_t supplementaryBase = 0x10000U;

        const std::uint32_t high = static_cast<std::uint32_t>(first) - highBase;
        const std::uint32_t low = static_cast<std::uint32_t>(second) - lowBase;
        const char32_t scalar = static_cast<char32_t>(supplementaryBase + (high << 10U) + low);
        return {.scalar = scalar, .codeUnitsConsumed = 2, .outcome = Types::DecodeOutcome::Decoded};
    }

    /// @brief Returns the UTF-16 code-unit count for a known-valid Unicode scalar.
    [[nodiscard]] constexpr std::uint8_t utf16EncodedLength(char32_t scalar) noexcept
    {
        return scalar <= static_cast<char32_t>(0xFFFF) ? 1 : 2;
    }

    /// @brief Encodes one known-valid scalar into at least two writable UTF-16 code units.
    inline void encodeUtf16Unchecked(char32_t scalar, char16_t *destination) noexcept
    {
        const std::uint32_t value = static_cast<std::uint32_t>(scalar);
        if (value <= 0xFFFFU)
        {
            destination[0] = static_cast<char16_t>(value);
            return;
        }

        const std::uint32_t supplementary = value - 0x10000U;
        destination[0] = static_cast<char16_t>(0xD800U + (supplementary >> 10U));
        destination[1] = static_cast<char16_t>(0xDC00U + (supplementary & 0x3FFU));
    }

    /// @brief Result of decoding the complete scalar immediately before a byte offset.
    struct PreviousUtf8ScalarResult
    {
        Types::Utf8DecodeResult decoded{};
        std::size_t startOffset = 0;
    };

    /// @brief Decodes the scalar ending exactly at one nonzero byte offset.
    [[nodiscard]] inline PreviousUtf8ScalarResult decodePreviousUtf8Scalar(std::string_view text, std::size_t byteOffset) noexcept
    {
        if (byteOffset == 0 || byteOffset > text.size())
        {
            return {};
        }

        std::size_t startOffset = byteOffset - 1;
        while (startOffset > 0 && isContinuationByte(text[startOffset]))
        {
            --startOffset;
        }

        const std::size_t encodedLength = byteOffset - startOffset;
        const Types::Utf8DecodeResult decoded = decodeUtf8Scalar(text.substr(startOffset, encodedLength));

        if (decoded.outcome != Types::DecodeOutcome::Decoded || decoded.bytesConsumed != encodedLength)
        {
            return {
                .decoded = {.outcome = Types::DecodeOutcome::InvalidEncoding},
                .startOffset = byteOffset,
            };
        }

        return {.decoded = decoded, .startOffset = startOffset};
    }
} // namespace GameWIP::Unicode::Internal
