/// @file cursor_selection.h
/// @brief Private custom cursor DPI variant selection policy.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameWIP::Window::Detail
{
    /// @brief Returns whether one intended DPI is a better match than the current candidate.
    [[nodiscard]] constexpr bool isBetterDpiCandidate(std::uint32_t candidateDpi, std::uint32_t currentDpi, std::uint32_t targetDpi) noexcept
    {
        const std::uint32_t candidateDistance = candidateDpi > targetDpi ? candidateDpi - targetDpi : targetDpi - candidateDpi;

        const std::uint32_t currentDistance = currentDpi > targetDpi ? currentDpi - targetDpi : targetDpi - currentDpi;

        return candidateDistance < currentDistance || (candidateDistance == currentDistance && candidateDpi > currentDpi);
    }

    /// @brief Selects the nearest intended DPI, preferring the higher DPI on a tie.
    [[nodiscard]] constexpr std::size_t selectDpiVariantIndex(std::span<const std::uint32_t> intendedDpis, std::uint32_t targetDpi) noexcept
    {
        std::size_t best = 0;

        for (std::size_t i = 1; i < intendedDpis.size(); ++i)
        {
            if (isBetterDpiCandidate(intendedDpis[i], intendedDpis[best], targetDpi))
            {
                best = i;
            }
        }

        return best;
    }
} // namespace GameWIP::Window::Detail
