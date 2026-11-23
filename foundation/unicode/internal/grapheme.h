/// @file grapheme.h
/// @brief Private grapheme-boundary helpers shared by the Unicode implementation.

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace GameWIP::Unicode::Types::Utf8
{
    struct BoundaryResult;
}

namespace GameWIP::Unicode::Internal::Grapheme
{
    /// @brief Scans once, writes boundaries that fit, and reports the complete required count.
    [[nodiscard]] bool writeBoundaries(std::string_view text, std::span<std::size_t> boundaryStorage, std::size_t &requiredBoundaryCount) noexcept;

    [[nodiscard]] Types::Utf8::BoundaryResult nextBoundary(std::string_view text, std::size_t byteOffset) noexcept;
    [[nodiscard]] Types::Utf8::BoundaryResult previousBoundary(std::string_view text, std::size_t byteOffset) noexcept;
} // namespace GameWIP::Unicode::Internal::Grapheme
