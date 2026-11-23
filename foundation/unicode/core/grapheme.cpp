/// @file grapheme.cpp
/// @brief Unicode 17.0.0 default extended grapheme-cluster boundary traversal.

#include "unicode/unicode.h"
#include "unicode/internal/encoding.h"
#include "unicode/internal/grapheme.h"
#include "unicode/internal/unicode_properties.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace GameWIP::Unicode::Internal::Grapheme
{
    namespace UnicodeInternal = ::GameWIP::Unicode::Internal;
    namespace UnicodeUtf8 = ::GameWIP::Unicode::Utf8;

    namespace
    {
        /// ------------------------------------------------------------
        /// Grapheme break rules
        /// ------------------------------------------------------------

        enum class IndicSequenceState : std::uint8_t
        {
            None,
            Consonant,
            Linked,
        };

        enum class EmojiSequenceState : std::uint8_t
        {
            None,
            ExtendedPictographic,
            ExtendedPictographicZwj,
        };

        /// @brief Streaming context required by the context-sensitive grapheme rules.
        struct GraphemeState
        {
            IndicSequenceState indic = IndicSequenceState::None;
            EmojiSequenceState emoji = EmojiSequenceState::None;
            std::size_t regionalIndicatorCount = 0;

            /// @brief Incorporates one decoded scalar into the rule context.
            void consume(const UnicodeInternal::UnicodeProperties &properties) noexcept
            {
                switch(properties.indicConjunctBreak)
                {
                case UnicodeInternal::IndicConjunctBreakClass::Consonant:
                    indic = IndicSequenceState::Consonant;
                    break;
                case UnicodeInternal::IndicConjunctBreakClass::Extend:
                    break;
                case UnicodeInternal::IndicConjunctBreakClass::Linker:
                    indic = indic == IndicSequenceState::None ? IndicSequenceState::None : IndicSequenceState::Linked;
                    break;
                case UnicodeInternal::IndicConjunctBreakClass::None:
                    indic = IndicSequenceState::None;
                    break;
                }

                if(properties.extendedPictographic)
                {
                    emoji = EmojiSequenceState::ExtendedPictographic;
                }
                else if(properties.graphemeBreak == UnicodeInternal::GraphemeBreakClass::Extend && emoji == EmojiSequenceState::ExtendedPictographic)
                {
                    // GB11 permits any number of Extend scalars before the ZWJ.
                }
                else if(properties.graphemeBreak == UnicodeInternal::GraphemeBreakClass::ZWJ && emoji == EmojiSequenceState::ExtendedPictographic)
                {
                    emoji = EmojiSequenceState::ExtendedPictographicZwj;
                }
                else
                {
                    emoji = EmojiSequenceState::None;
                }

                if(properties.graphemeBreak == UnicodeInternal::GraphemeBreakClass::RegionalIndicator)
                {
                    ++regionalIndicatorCount;
                }
                else
                {
                    regionalIndicatorCount = 0;
                }
            }
        };

        /// @brief Returns whether a grapheme class is handled by GB4 or GB5.
        [[nodiscard]] bool isControlClass(UnicodeInternal::GraphemeBreakClass value) noexcept
        {
            return value == UnicodeInternal::GraphemeBreakClass::Control || value == UnicodeInternal::GraphemeBreakClass::CR ||
                   value == UnicodeInternal::GraphemeBreakClass::LF;
        }

        /// @brief Applies the ordered Unicode 17 extended grapheme-cluster rules at one boundary.
        [[nodiscard]] bool shouldBreak(
            const UnicodeInternal::UnicodeProperties &previous,
            const UnicodeInternal::UnicodeProperties &current,
            const GraphemeState &state) noexcept
        {
            using Class = UnicodeInternal::GraphemeBreakClass;

            // GB3
            if(previous.graphemeBreak == Class::CR && current.graphemeBreak == Class::LF)
            {
                return false;
            }

            // GB4 and GB5
            if(isControlClass(previous.graphemeBreak) || isControlClass(current.graphemeBreak))
            {
                return true;
            }

            // GB6
            if(previous.graphemeBreak == Class::L && (current.graphemeBreak == Class::L || current.graphemeBreak == Class::V ||
                                                      current.graphemeBreak == Class::LV || current.graphemeBreak == Class::LVT))
            {
                return false;
            }

            // GB7
            if((previous.graphemeBreak == Class::LV || previous.graphemeBreak == Class::V) &&
               (current.graphemeBreak == Class::V || current.graphemeBreak == Class::T))
            {
                return false;
            }

            // GB8
            if((previous.graphemeBreak == Class::LVT || previous.graphemeBreak == Class::T) && current.graphemeBreak == Class::T)
            {
                return false;
            }

            // GB9
            if(current.graphemeBreak == Class::Extend || current.graphemeBreak == Class::ZWJ)
            {
                return false;
            }

            // GB9a
            if(current.graphemeBreak == Class::SpacingMark)
            {
                return false;
            }

            // GB9b
            if(previous.graphemeBreak == Class::Prepend)
            {
                return false;
            }

            // GB9c
            if(current.indicConjunctBreak == UnicodeInternal::IndicConjunctBreakClass::Consonant && state.indic == IndicSequenceState::Linked)
            {
                return false;
            }

            // GB11
            if(current.extendedPictographic && state.emoji == EmojiSequenceState::ExtendedPictographicZwj)
            {
                return false;
            }

            // GB12 and GB13
            if(previous.graphemeBreak == Class::RegionalIndicator && current.graphemeBreak == Class::RegionalIndicator &&
               (state.regionalIndicatorCount & 1U) != 0)
            {
                return false;
            }

            // GB999
            return true;
        }

        /// ------------------------------------------------------------
        /// Boundary scan helpers
        /// ------------------------------------------------------------

        /// @brief Attempts the common ASCII-only next-boundary path without table lookup.
        [[nodiscard]] Types::Utf8::BoundaryResult nextAsciiBoundary(std::string_view text, std::size_t byteOffset) noexcept
        {
            const std::uint8_t current = UnicodeInternal::byteValue(text[byteOffset]);

            if(current >= 0x80U)
            {
                return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
            }

            const bool previousIsAscii = byteOffset == 0 || UnicodeInternal::byteValue(text[byteOffset - 1]) < 0x80U;

            if(!previousIsAscii)
            {
                return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
            }

            if(current == 0x0DU && byteOffset + 1 < text.size() && text[byteOffset + 1] == '\n')
            {
                return {.byteOffset = byteOffset + 2, .outcome = Types::Utf8::BoundaryOutcome::Found};
            }
            const bool currentIsControl = current <= 0x1FU || current == 0x7FU;
            const bool nextIsAscii = byteOffset + 1 == text.size() || UnicodeInternal::byteValue(text[byteOffset + 1]) < 0x80U;

            if(currentIsControl || nextIsAscii)
            {
                return {.byteOffset = byteOffset + 1, .outcome = Types::Utf8::BoundaryOutcome::Found};
            }

            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        /// @brief Attempts the common ASCII-only previous-boundary path without table lookup.
        [[nodiscard]] Types::Utf8::BoundaryResult previousAsciiBoundary(std::string_view text, std::size_t byteOffset) noexcept
        {
            const std::size_t previousOffset = byteOffset - 1;
            const std::uint8_t previous = UnicodeInternal::byteValue(text[previousOffset]);

            if(previous >= 0x80U)
            {
                return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
            }

            if(previous == 0x0AU && previousOffset > 0 && text[previousOffset - 1] == '\r')
            {
                return {.byteOffset = previousOffset - 1, .outcome = Types::Utf8::BoundaryOutcome::Found};
            }
            const bool beforePreviousIsAscii = previousOffset == 0 || UnicodeInternal::byteValue(text[previousOffset - 1]) < 0x80U;

            if(beforePreviousIsAscii)
            {
                return {.byteOffset = previousOffset, .outcome = Types::Utf8::BoundaryOutcome::Found};
            }

            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        /// @brief Returns whether a boundary is guaranteed without earlier grapheme state.
        [[nodiscard]] bool isGuaranteedBreak(
            const UnicodeInternal::UnicodeProperties &previous,
            const UnicodeInternal::UnicodeProperties &current) noexcept
        {
            using Class = UnicodeInternal::GraphemeBreakClass;

            if(previous.graphemeBreak == Class::CR && current.graphemeBreak == Class::LF)
            {
                return false;
            }

            if(isControlClass(previous.graphemeBreak) || isControlClass(current.graphemeBreak))
            {
                return true;
            }

            if(previous.graphemeBreak == Class::L && (current.graphemeBreak == Class::L || current.graphemeBreak == Class::V ||
                                                      current.graphemeBreak == Class::LV || current.graphemeBreak == Class::LVT))
            {
                return false;
            }

            if((previous.graphemeBreak == Class::LV || previous.graphemeBreak == Class::V) &&
               (current.graphemeBreak == Class::V || current.graphemeBreak == Class::T))
            {
                return false;
            }

            if((previous.graphemeBreak == Class::LVT || previous.graphemeBreak == Class::T) && current.graphemeBreak == Class::T)
            {
                return false;
            }

            if(current.graphemeBreak == Class::Extend || current.graphemeBreak == Class::ZWJ || current.graphemeBreak == Class::SpacingMark ||
               previous.graphemeBreak == Class::Prepend)
            {
                return false;
            }

            // GB9c, GB11, GB12, and GB13 depend on state preceding this pair.
            if(current.indicConjunctBreak == UnicodeInternal::IndicConjunctBreakClass::Consonant || current.extendedPictographic ||
               (previous.graphemeBreak == Class::RegionalIndicator && current.graphemeBreak == Class::RegionalIndicator))
            {
                return false;
            }

            return true;
        }

        struct RestartResult
        {
            std::size_t byteOffset = 0;
            bool valid = true;
        };

        /// ------------------------------------------------------------
        /// Incremental boundary scans
        /// ------------------------------------------------------------

        /// @brief Finds the nearest earlier boundary from which grapheme state can be rebuilt independently.
        [[nodiscard]] RestartResult findSafeRestart(std::string_view text, std::size_t scalarOffset) noexcept
        {
            std::size_t currentOffset = scalarOffset;

            while(currentOffset > 0)
            {
                const Types::Utf8::DecodeResult currentDecoded = UnicodeInternal::decodeUtf8Scalar(text.substr(currentOffset));

                if(currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = scalarOffset, .valid = false};
                }
                const UnicodeInternal::PreviousUtf8ScalarResult previousDecoded = UnicodeInternal::decodePreviousUtf8Scalar(text, currentOffset);

                if(previousDecoded.decoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = scalarOffset, .valid = false};
                }
                const UnicodeInternal::UnicodeProperties previousProperties = UnicodeInternal::unicodeProperties(previousDecoded.decoded.scalar);
                const UnicodeInternal::UnicodeProperties currentProperties = UnicodeInternal::unicodeProperties(currentDecoded.scalar);

                if(isGuaranteedBreak(previousProperties, currentProperties))
                {
                    return {.byteOffset = currentOffset};
                }
                currentOffset = previousDecoded.startOffset;
            }

            return {};
        }

        /// @brief Visits every grapheme boundary in one complete left-to-right segmentation pass.
        template <typename BoundaryVisitor> [[nodiscard]] bool visitGraphemeBoundaries(std::string_view text, BoundaryVisitor &visitor) noexcept
        {
            visitor(std::size_t{0});

            if(text.empty())
            {
                return true;
            }

            std::size_t currentOffset = 0;
            Types::Utf8::DecodeResult previousDecoded = UnicodeInternal::decodeUtf8Scalar(text);

            if(previousDecoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return false;
            }
            UnicodeInternal::UnicodeProperties previousProperties = UnicodeInternal::unicodeProperties(previousDecoded.scalar);
            GraphemeState state{};
            state.consume(previousProperties);
            currentOffset += previousDecoded.bytesConsumed;

            while(currentOffset < text.size())
            {
                const Types::Utf8::DecodeResult currentDecoded = UnicodeInternal::decodeUtf8Scalar(text.substr(currentOffset));

                if(currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return false;
                }
                const UnicodeInternal::UnicodeProperties currentProperties = UnicodeInternal::unicodeProperties(currentDecoded.scalar);

                if(shouldBreak(previousProperties, currentProperties, state))
                {
                    visitor(currentOffset);
                }

                state.consume(currentProperties);
                previousProperties = currentProperties;
                currentOffset += currentDecoded.bytesConsumed;
            }

            visitor(text.size());
            return true;
        }

        /// @brief Finds the first grapheme boundary after target using state rebuilt from a safe restart.
        [[nodiscard]] Types::Utf8::BoundaryResult scanNextFrom(std::string_view text, std::size_t restartOffset, std::size_t targetOffset) noexcept
        {
            Types::Utf8::DecodeResult previousDecoded = UnicodeInternal::decodeUtf8Scalar(text.substr(restartOffset));

            if(previousDecoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return {.byteOffset = targetOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
            }
            UnicodeInternal::UnicodeProperties previousProperties = UnicodeInternal::unicodeProperties(previousDecoded.scalar);
            GraphemeState state{};
            state.consume(previousProperties);
            std::size_t currentOffset = restartOffset + previousDecoded.bytesConsumed;

            while(currentOffset < text.size())
            {
                const Types::Utf8::DecodeResult currentDecoded = UnicodeInternal::decodeUtf8Scalar(text.substr(currentOffset));

                if(currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = targetOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
                }
                const UnicodeInternal::UnicodeProperties currentProperties = UnicodeInternal::unicodeProperties(currentDecoded.scalar);

                if(shouldBreak(previousProperties, currentProperties, state) && currentOffset > targetOffset)
                {
                    return {.byteOffset = currentOffset, .outcome = Types::Utf8::BoundaryOutcome::Found};
                }
                state.consume(currentProperties);
                previousProperties = currentProperties;
                currentOffset += currentDecoded.bytesConsumed;
            }

            return {.byteOffset = text.size(), .outcome = Types::Utf8::BoundaryOutcome::Found};
        }

        /// @brief Finds the last grapheme boundary before target using state rebuilt from a safe restart.
        [[nodiscard]] Types::Utf8::BoundaryResult scanPreviousFrom(
            std::string_view text,
            std::size_t restartOffset,
            std::size_t targetOffset) noexcept
        {
            std::size_t previousBoundary = restartOffset;
            Types::Utf8::DecodeResult previousDecoded = UnicodeInternal::decodeUtf8Scalar(text.substr(restartOffset));

            if(previousDecoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return {.byteOffset = targetOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
            }
            UnicodeInternal::UnicodeProperties previousProperties = UnicodeInternal::unicodeProperties(previousDecoded.scalar);
            GraphemeState state{};
            state.consume(previousProperties);
            std::size_t currentOffset = restartOffset + previousDecoded.bytesConsumed;

            while(currentOffset < text.size())
            {
                const Types::Utf8::DecodeResult currentDecoded = UnicodeInternal::decodeUtf8Scalar(text.substr(currentOffset));

                if(currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = targetOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
                }
                const UnicodeInternal::UnicodeProperties currentProperties = UnicodeInternal::unicodeProperties(currentDecoded.scalar);

                if(shouldBreak(previousProperties, currentProperties, state))
                {
                    if(currentOffset >= targetOffset)
                    {
                        return {.byteOffset = previousBoundary, .outcome = Types::Utf8::BoundaryOutcome::Found};
                    }
                    previousBoundary = currentOffset;
                }

                state.consume(currentProperties);
                previousProperties = currentProperties;
                currentOffset += currentDecoded.bytesConsumed;
            }

            return {.byteOffset = previousBoundary, .outcome = Types::Utf8::BoundaryOutcome::Found};
        }
    } // namespace

    /// @brief Writes grapheme boundaries into caller storage and reports the required count.
    [[nodiscard]] bool writeBoundaries(std::string_view text, std::span<std::size_t> boundaryStorage, std::size_t &requiredBoundaryCount) noexcept
    {
        requiredBoundaryCount = 0;
        auto recordBoundary = [&](std::size_t byteOffset) noexcept
        {
            if(requiredBoundaryCount < boundaryStorage.size())
            {
                boundaryStorage[requiredBoundaryCount] = byteOffset;
            }
            ++requiredBoundaryCount;
        };

        return visitGraphemeBoundaries(text, recordBoundary);
    }

    Types::Utf8::BoundaryResult nextBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        if(byteOffset > text.size())
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        if(byteOffset < text.size() && UnicodeInternal::isContinuationByte(text[byteOffset]))
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        if(byteOffset == text.size())
        {
            const Types::Utf8::ValidationResult validation = UnicodeUtf8::validate(text);

            if(validation.outcome != Types::ValidationOutcome::Valid)
            {
                return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
            }
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::AtEnd};
        }

        if(UnicodeInternal::byteValue(text[byteOffset]) < 0x80U)
        {
            const Types::Utf8::BoundaryResult ascii = nextAsciiBoundary(text, byteOffset);

            if(ascii.outcome == Types::Utf8::BoundaryOutcome::Found)
            {
                return ascii;
            }
        }

        const RestartResult restart = findSafeRestart(text, byteOffset);

        if(!restart.valid)
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
        }
        return scanNextFrom(text, restart.byteOffset, byteOffset);
    }

    Types::Utf8::BoundaryResult previousBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        if(byteOffset > text.size())
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        if(byteOffset < text.size() && UnicodeInternal::isContinuationByte(text[byteOffset]))
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        if(byteOffset == 0)
        {
            return {.byteOffset = 0, .outcome = Types::Utf8::BoundaryOutcome::AtBeginning};
        }

        if(UnicodeInternal::byteValue(text[byteOffset - 1]) < 0x80U)
        {
            const Types::Utf8::BoundaryResult ascii = previousAsciiBoundary(text, byteOffset);

            if(ascii.outcome == Types::Utf8::BoundaryOutcome::Found)
            {
                return ascii;
            }
        }

        const UnicodeInternal::PreviousUtf8ScalarResult previousScalar = UnicodeInternal::decodePreviousUtf8Scalar(text, byteOffset);

        if(previousScalar.decoded.outcome != Types::DecodeOutcome::Decoded)
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
        }
        const RestartResult restart = findSafeRestart(text, previousScalar.startOffset);

        if(!restart.valid)
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidEncoding};
        }
        return scanPreviousFrom(text, restart.byteOffset, byteOffset);
    }

} // namespace GameWIP::Unicode::Internal::Grapheme

namespace GameWIP::Unicode::Utf8
{
    Types::Utf8::BoundaryResult nextGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        return Internal::Grapheme::nextBoundary(text, byteOffset);
    }

    Types::Utf8::BoundaryResult previousGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        return Internal::Grapheme::previousBoundary(text, byteOffset);
    }
} // namespace GameWIP::Unicode::Utf8
