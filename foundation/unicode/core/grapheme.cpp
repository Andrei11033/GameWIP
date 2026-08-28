/// @file grapheme.cpp
/// @brief Unicode 17.0.0 default extended grapheme-cluster boundary traversal.

#include "unicode/unicode.h"
#include "unicode/internal/encoding.h"
#include "unicode/internal/unicode_properties.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GameWIP::Unicode::Utf8
{
    namespace
    {
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
            void consume(const Internal::UnicodeProperties &properties) noexcept
            {
                switch (properties.indicConjunctBreak)
                {
                case Internal::IndicConjunctBreakClass::Consonant:
                    indic = IndicSequenceState::Consonant;
                    break;
                case Internal::IndicConjunctBreakClass::Extend:
                    break;
                case Internal::IndicConjunctBreakClass::Linker:
                    indic = indic == IndicSequenceState::None ? IndicSequenceState::None : IndicSequenceState::Linked;
                    break;
                case Internal::IndicConjunctBreakClass::None:
                    indic = IndicSequenceState::None;
                    break;
                }

                if (properties.extendedPictographic)
                {
                    emoji = EmojiSequenceState::ExtendedPictographic;
                }
                else if (properties.graphemeBreak == Internal::GraphemeBreakClass::Extend && emoji == EmojiSequenceState::ExtendedPictographic)
                {
                    // GB11 permits any number of Extend scalars before the ZWJ.
                }
                else if (properties.graphemeBreak == Internal::GraphemeBreakClass::ZWJ && emoji == EmojiSequenceState::ExtendedPictographic)
                {
                    emoji = EmojiSequenceState::ExtendedPictographicZwj;
                }
                else
                {
                    emoji = EmojiSequenceState::None;
                }

                if (properties.graphemeBreak == Internal::GraphemeBreakClass::RegionalIndicator)
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
        [[nodiscard]] bool isControlClass(Internal::GraphemeBreakClass value) noexcept
        {
            return value == Internal::GraphemeBreakClass::Control || value == Internal::GraphemeBreakClass::CR ||
                   value == Internal::GraphemeBreakClass::LF;
        }

        /// @brief Applies the ordered Unicode 17 extended grapheme-cluster rules at one boundary.
        [[nodiscard]] bool shouldBreak(
            const Internal::UnicodeProperties &previous,
            const Internal::UnicodeProperties &current,
            const GraphemeState &state) noexcept
        {
            using Class = Internal::GraphemeBreakClass;

            // GB3
            if (previous.graphemeBreak == Class::CR && current.graphemeBreak == Class::LF)
            {
                return false;
            }

            // GB4 and GB5
            if (isControlClass(previous.graphemeBreak) || isControlClass(current.graphemeBreak))
            {
                return true;
            }

            // GB6
            if (previous.graphemeBreak == Class::L && (current.graphemeBreak == Class::L || current.graphemeBreak == Class::V ||
                                                       current.graphemeBreak == Class::LV || current.graphemeBreak == Class::LVT))
            {
                return false;
            }

            // GB7
            if ((previous.graphemeBreak == Class::LV || previous.graphemeBreak == Class::V) &&
                (current.graphemeBreak == Class::V || current.graphemeBreak == Class::T))
            {
                return false;
            }

            // GB8
            if ((previous.graphemeBreak == Class::LVT || previous.graphemeBreak == Class::T) && current.graphemeBreak == Class::T)
            {
                return false;
            }

            // GB9
            if (current.graphemeBreak == Class::Extend || current.graphemeBreak == Class::ZWJ)
            {
                return false;
            }

            // GB9a
            if (current.graphemeBreak == Class::SpacingMark)
            {
                return false;
            }

            // GB9b
            if (previous.graphemeBreak == Class::Prepend)
            {
                return false;
            }

            // GB9c
            if (current.indicConjunctBreak == Internal::IndicConjunctBreakClass::Consonant && state.indic == IndicSequenceState::Linked)
            {
                return false;
            }

            // GB11
            if (current.extendedPictographic && state.emoji == EmojiSequenceState::ExtendedPictographicZwj)
            {
                return false;
            }

            // GB12 and GB13
            if (previous.graphemeBreak == Class::RegionalIndicator && current.graphemeBreak == Class::RegionalIndicator &&
                (state.regionalIndicatorCount & 1U) != 0)
            {
                return false;
            }

            // GB999
            return true;
        }

        /// @brief Attempts the common ASCII-only next-boundary path without table lookup.
        [[nodiscard]] Types::Utf8BoundaryResult nextAsciiBoundary(std::string_view text, std::size_t byteOffset) noexcept
        {
            const std::uint8_t current = Internal::byteValue(text[byteOffset]);
            if (current >= 0x80U)
            {
                return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
            }

            const bool previousIsAscii = byteOffset == 0 || Internal::byteValue(text[byteOffset - 1]) < 0x80U;
            if (!previousIsAscii)
            {
                return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
            }

            if (current == 0x0DU && byteOffset + 1 < text.size() && text[byteOffset + 1] == '\n')
            {
                return {.byteOffset = byteOffset + 2, .outcome = Types::BoundaryOutcome::Found};
            }

            const bool currentIsControl = current <= 0x1FU || current == 0x7FU;
            const bool nextIsAscii = byteOffset + 1 == text.size() || Internal::byteValue(text[byteOffset + 1]) < 0x80U;
            if (currentIsControl || nextIsAscii)
            {
                return {.byteOffset = byteOffset + 1, .outcome = Types::BoundaryOutcome::Found};
            }

            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }

        /// @brief Attempts the common ASCII-only previous-boundary path without table lookup.
        [[nodiscard]] Types::Utf8BoundaryResult previousAsciiBoundary(std::string_view text, std::size_t byteOffset) noexcept
        {
            const std::size_t previousOffset = byteOffset - 1;
            const std::uint8_t previous = Internal::byteValue(text[previousOffset]);
            if (previous >= 0x80U)
            {
                return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
            }

            if (previous == 0x0AU && previousOffset > 0 && text[previousOffset - 1] == '\r')
            {
                return {.byteOffset = previousOffset - 1, .outcome = Types::BoundaryOutcome::Found};
            }

            const bool beforePreviousIsAscii = previousOffset == 0 || Internal::byteValue(text[previousOffset - 1]) < 0x80U;
            if (beforePreviousIsAscii)
            {
                return {.byteOffset = previousOffset, .outcome = Types::BoundaryOutcome::Found};
            }

            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }

        /// @brief Returns whether a boundary is guaranteed without earlier grapheme state.
        [[nodiscard]] bool isGuaranteedBreak(const Internal::UnicodeProperties &previous, const Internal::UnicodeProperties &current) noexcept
        {
            using Class = Internal::GraphemeBreakClass;

            if (previous.graphemeBreak == Class::CR && current.graphemeBreak == Class::LF)
            {
                return false;
            }
            if (isControlClass(previous.graphemeBreak) || isControlClass(current.graphemeBreak))
            {
                return true;
            }
            if (previous.graphemeBreak == Class::L && (current.graphemeBreak == Class::L || current.graphemeBreak == Class::V ||
                                                       current.graphemeBreak == Class::LV || current.graphemeBreak == Class::LVT))
            {
                return false;
            }
            if ((previous.graphemeBreak == Class::LV || previous.graphemeBreak == Class::V) &&
                (current.graphemeBreak == Class::V || current.graphemeBreak == Class::T))
            {
                return false;
            }
            if ((previous.graphemeBreak == Class::LVT || previous.graphemeBreak == Class::T) && current.graphemeBreak == Class::T)
            {
                return false;
            }
            if (current.graphemeBreak == Class::Extend || current.graphemeBreak == Class::ZWJ || current.graphemeBreak == Class::SpacingMark ||
                previous.graphemeBreak == Class::Prepend)
            {
                return false;
            }

            // GB9c, GB11, GB12, and GB13 depend on state preceding this pair.
            if (current.indicConjunctBreak == Internal::IndicConjunctBreakClass::Consonant || current.extendedPictographic ||
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

        /// @brief Finds the nearest earlier boundary from which grapheme state can be rebuilt independently.
        [[nodiscard]] RestartResult findSafeRestart(std::string_view text, std::size_t scalarOffset) noexcept
        {
            std::size_t currentOffset = scalarOffset;
            while (currentOffset > 0)
            {
                const Types::Utf8DecodeResult currentDecoded = Internal::decodeUtf8Scalar(text.substr(currentOffset));
                if (currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = scalarOffset, .valid = false};
                }

                const Internal::PreviousUtf8ScalarResult previousDecoded = Internal::decodePreviousUtf8Scalar(text, currentOffset);
                if (previousDecoded.decoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = scalarOffset, .valid = false};
                }

                const Internal::UnicodeProperties previousProperties = Internal::unicodeProperties(previousDecoded.decoded.scalar);
                const Internal::UnicodeProperties currentProperties = Internal::unicodeProperties(currentDecoded.scalar);
                if (isGuaranteedBreak(previousProperties, currentProperties))
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
            if (text.empty())
            {
                return true;
            }

            std::size_t currentOffset = 0;
            Types::Utf8DecodeResult previousDecoded = Internal::decodeUtf8Scalar(text);
            if (previousDecoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return false;
            }

            Internal::UnicodeProperties previousProperties = Internal::unicodeProperties(previousDecoded.scalar);
            GraphemeState state{};
            state.consume(previousProperties);
            currentOffset += previousDecoded.bytesConsumed;

            while (currentOffset < text.size())
            {
                const Types::Utf8DecodeResult currentDecoded = Internal::decodeUtf8Scalar(text.substr(currentOffset));
                if (currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return false;
                }

                const Internal::UnicodeProperties currentProperties = Internal::unicodeProperties(currentDecoded.scalar);
                if (shouldBreak(previousProperties, currentProperties, state))
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
        [[nodiscard]] Types::Utf8BoundaryResult scanNextFrom(std::string_view text, std::size_t restartOffset, std::size_t targetOffset) noexcept
        {
            Types::Utf8DecodeResult previousDecoded = Internal::decodeUtf8Scalar(text.substr(restartOffset));
            if (previousDecoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return {.byteOffset = targetOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
            }

            Internal::UnicodeProperties previousProperties = Internal::unicodeProperties(previousDecoded.scalar);
            GraphemeState state{};
            state.consume(previousProperties);
            std::size_t currentOffset = restartOffset + previousDecoded.bytesConsumed;

            while (currentOffset < text.size())
            {
                const Types::Utf8DecodeResult currentDecoded = Internal::decodeUtf8Scalar(text.substr(currentOffset));
                if (currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = targetOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
                }

                const Internal::UnicodeProperties currentProperties = Internal::unicodeProperties(currentDecoded.scalar);
                if (shouldBreak(previousProperties, currentProperties, state) && currentOffset > targetOffset)
                {
                    return {.byteOffset = currentOffset, .outcome = Types::BoundaryOutcome::Found};
                }

                state.consume(currentProperties);
                previousProperties = currentProperties;
                currentOffset += currentDecoded.bytesConsumed;
            }

            return {.byteOffset = text.size(), .outcome = Types::BoundaryOutcome::Found};
        }

        /// @brief Finds the last grapheme boundary before target using state rebuilt from a safe restart.
        [[nodiscard]] Types::Utf8BoundaryResult scanPreviousFrom(std::string_view text, std::size_t restartOffset, std::size_t targetOffset) noexcept
        {
            std::size_t previousBoundary = restartOffset;
            Types::Utf8DecodeResult previousDecoded = Internal::decodeUtf8Scalar(text.substr(restartOffset));
            if (previousDecoded.outcome != Types::DecodeOutcome::Decoded)
            {
                return {.byteOffset = targetOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
            }

            Internal::UnicodeProperties previousProperties = Internal::unicodeProperties(previousDecoded.scalar);
            GraphemeState state{};
            state.consume(previousProperties);
            std::size_t currentOffset = restartOffset + previousDecoded.bytesConsumed;

            while (currentOffset < text.size())
            {
                const Types::Utf8DecodeResult currentDecoded = Internal::decodeUtf8Scalar(text.substr(currentOffset));
                if (currentDecoded.outcome != Types::DecodeOutcome::Decoded)
                {
                    return {.byteOffset = targetOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
                }

                const Internal::UnicodeProperties currentProperties = Internal::unicodeProperties(currentDecoded.scalar);
                if (shouldBreak(previousProperties, currentProperties, state))
                {
                    if (currentOffset >= targetOffset)
                    {
                        return {.byteOffset = previousBoundary, .outcome = Types::BoundaryOutcome::Found};
                    }
                    previousBoundary = currentOffset;
                }

                state.consume(currentProperties);
                previousProperties = currentProperties;
                currentOffset += currentDecoded.bytesConsumed;
            }

            return {.byteOffset = previousBoundary, .outcome = Types::BoundaryOutcome::Found};
        }
    } // namespace

    Types::Utf8GraphemeIndexResult GraphemeCursor::reset(std::string_view text, std::span<std::size_t> boundaryStorage) noexcept
    {
        clear();

        std::size_t requiredBoundaryCount = 0;
        auto recordBoundary = [&](std::size_t byteOffset) noexcept
        {
            if (requiredBoundaryCount < boundaryStorage.size())
            {
                boundaryStorage[requiredBoundaryCount] = byteOffset;
            }
            ++requiredBoundaryCount;
        };

        if (!visitGraphemeBoundaries(text, recordBoundary))
        {
            return {.outcome = Types::GraphemeIndexOutcome::InvalidEncoding};
        }

        if (requiredBoundaryCount > boundaryStorage.size())
        {
            return {
                .requiredBoundaryCount = requiredBoundaryCount,
                .outcome = Types::GraphemeIndexOutcome::DestinationTooSmall,
            };
        }

        boundaries_ = boundaryStorage.first(requiredBoundaryCount);
        currentBoundaryIndex_ = 0;
        return {
            .requiredBoundaryCount = requiredBoundaryCount,
            .outcome = Types::GraphemeIndexOutcome::Indexed,
        };
    }

    void GraphemeCursor::clear() noexcept
    {
        boundaries_ = {};
        currentBoundaryIndex_ = 0;
    }

    bool GraphemeCursor::ready() const noexcept
    {
        return !boundaries_.empty();
    }

    std::size_t GraphemeCursor::byteOffset() const noexcept
    {
        return ready() ? boundaries_[currentBoundaryIndex_] : 0;
    }

    std::size_t GraphemeCursor::boundaryCount() const noexcept
    {
        return boundaries_.size();
    }

    Types::Utf8BoundaryResult GraphemeCursor::seek(std::size_t byteOffset) noexcept
    {
        if (!ready())
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }

        const auto found = std::lower_bound(boundaries_.begin(), boundaries_.end(), byteOffset);
        if (found == boundaries_.end() || *found != byteOffset)
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }

        currentBoundaryIndex_ = static_cast<std::size_t>(found - boundaries_.begin());
        return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::Found};
    }

    Types::Utf8BoundaryResult GraphemeCursor::next() noexcept
    {
        if (!ready())
        {
            return {.outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (currentBoundaryIndex_ + 1 >= boundaries_.size())
        {
            return {.byteOffset = byteOffset(), .outcome = Types::BoundaryOutcome::AtEnd};
        }

        ++currentBoundaryIndex_;
        return {.byteOffset = byteOffset(), .outcome = Types::BoundaryOutcome::Found};
    }

    Types::Utf8BoundaryResult GraphemeCursor::previous() noexcept
    {
        if (!ready())
        {
            return {.outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (currentBoundaryIndex_ == 0)
        {
            return {.byteOffset = 0, .outcome = Types::BoundaryOutcome::AtBeginning};
        }

        --currentBoundaryIndex_;
        return {.byteOffset = byteOffset(), .outcome = Types::BoundaryOutcome::Found};
    }

    void GraphemeCursor::discardAfterCurrent() noexcept
    {
        if (ready())
        {
            boundaries_ = boundaries_.first(currentBoundaryIndex_ + 1);
        }
    }

    Types::Utf8BoundaryResult nextGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept
    {
        if (byteOffset > text.size())
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (byteOffset < text.size() && Internal::isContinuationByte(text[byteOffset]))
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidOffset};
        }
        if (byteOffset == text.size())
        {
            const Types::Utf8ValidationResult validation = validate(text);
            if (validation.outcome != Types::ValidationOutcome::Valid)
            {
                return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
            }
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::AtEnd};
        }

        if (Internal::byteValue(text[byteOffset]) < 0x80U)
        {
            const Types::Utf8BoundaryResult ascii = nextAsciiBoundary(text, byteOffset);
            if (ascii.outcome == Types::BoundaryOutcome::Found)
            {
                return ascii;
            }
        }

        const RestartResult restart = findSafeRestart(text, byteOffset);
        if (!restart.valid)
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
        }
        return scanNextFrom(text, restart.byteOffset, byteOffset);
    }

    Types::Utf8BoundaryResult previousGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept
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

        if (Internal::byteValue(text[byteOffset - 1]) < 0x80U)
        {
            const Types::Utf8BoundaryResult ascii = previousAsciiBoundary(text, byteOffset);
            if (ascii.outcome == Types::BoundaryOutcome::Found)
            {
                return ascii;
            }
        }

        const Internal::PreviousUtf8ScalarResult previousScalar = Internal::decodePreviousUtf8Scalar(text, byteOffset);
        if (previousScalar.decoded.outcome != Types::DecodeOutcome::Decoded)
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
        }

        const RestartResult restart = findSafeRestart(text, previousScalar.startOffset);
        if (!restart.valid)
        {
            return {.byteOffset = byteOffset, .outcome = Types::BoundaryOutcome::InvalidEncoding};
        }
        return scanPreviousFrom(text, restart.byteOffset, byteOffset);
    }
} // namespace GameWIP::Unicode::Utf8
