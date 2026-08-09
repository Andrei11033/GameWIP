/// @file unicode_properties.h
/// @brief Internal constant-time Unicode grapheme-property lookup.

#pragma once

#include "unicode/internal/generated/unicode_properties.h"

#include <cstddef>
#include <cstdint>

namespace GameWIP::Unicode::Internal
{
    /// @brief Grapheme_Cluster_Break values used by Unicode extended grapheme segmentation.
    enum class GraphemeBreakClass : std::uint8_t
    {
        Other = 0,
        CR = 1,
        LF = 2,
        Control = 3,
        Extend = 4,
        ZWJ = 5,
        RegionalIndicator = 6,
        Prepend = 7,
        SpacingMark = 8,
        L = 9,
        V = 10,
        T = 11,
        LV = 12,
        LVT = 13,
    };

    /// @brief Indic_Conjunct_Break values used by Unicode rule GB9c.
    enum class IndicConjunctBreakClass : std::uint8_t
    {
        None = 0,
        Consonant = 1,
        Extend = 2,
        Linker = 3,
    };

    /// @brief Packed grapheme properties for one Unicode scalar.
    struct UnicodeProperties
    {
        GraphemeBreakClass graphemeBreak = GraphemeBreakClass::Other;
        IndicConjunctBreakClass indicConjunctBreak = IndicConjunctBreakClass::None;
        bool extendedPictographic = false;
    };

    /// @brief Returns algorithmic ASCII grapheme properties without consulting generated data.
    [[nodiscard]] constexpr UnicodeProperties asciiProperties(std::uint32_t codePoint) noexcept
    {
        if (codePoint == 0x0DU)
        {
            return {.graphemeBreak = GraphemeBreakClass::CR};
        }
        if (codePoint == 0x0AU)
        {
            return {.graphemeBreak = GraphemeBreakClass::LF};
        }
        if (codePoint <= 0x1FU || codePoint == 0x7FU)
        {
            return {.graphemeBreak = GraphemeBreakClass::Control};
        }
        return {};
    }

    /// @brief Returns algorithmic LV or LVT classification for a precomposed Hangul syllable.
    [[nodiscard]] constexpr UnicodeProperties hangulSyllableProperties(std::uint32_t codePoint) noexcept
    {
        constexpr std::uint32_t syllableBase = 0xAC00U;
        constexpr std::uint32_t trailingCount = 28U;
        const bool isLv = ((codePoint - syllableBase) % trailingCount) == 0;
        return {.graphemeBreak = isLv ? GraphemeBreakClass::LV : GraphemeBreakClass::LVT};
    }

    /// @brief Returns the generated grapheme properties for one Unicode scalar.
    [[nodiscard]] inline UnicodeProperties unicodeProperties(char32_t scalar) noexcept
    {
        const std::uint32_t codePoint = static_cast<std::uint32_t>(scalar);

        if (codePoint <= 0x7FU)
        {
            return asciiProperties(codePoint);
        }
        if (codePoint >= 0xAC00U && codePoint <= 0xD7A3U)
        {
            return hangulSyllableProperties(codePoint);
        }
        if (scalar >= Generated::kHighStart)
        {
            return {};
        }

        const std::size_t blockIndexOffset = static_cast<std::size_t>(codePoint) >> Generated::kBlockShift;
        const std::size_t blockIndex = Generated::kBlockIndexes[blockIndexOffset];
        const std::size_t propertyOffset = (blockIndex << Generated::kBlockShift) | (static_cast<std::size_t>(codePoint) & Generated::kBlockMask);
        const std::uint8_t packed = Generated::kPropertyBlocks[propertyOffset];

        return {
            .graphemeBreak = static_cast<GraphemeBreakClass>(packed & 0x0FU),
            .indicConjunctBreak = static_cast<IndicConjunctBreakClass>((packed >> 4U) & 0x03U),
            .extendedPictographic = (packed & 0x40U) != 0,
        };
    }
} // namespace GameWIP::Unicode::Internal
