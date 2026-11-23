/// @file win32_shell_helpers.h
/// @brief Shared private helpers for Desktop's Win32 shell features.

#pragma once

#include "desktop/internal/shell_state.h"
#include "io/status.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shared portable state
    /// ------------------------------------------------------------

    using Internal::OwnedIconImage;

    /// ------------------------------------------------------------
    /// Shared shell helpers
    /// ------------------------------------------------------------

    void retainShellMessageWindow() noexcept;
    void releaseShellMessageWindow() noexcept;

    namespace Win32
    {
        inline constexpr UINT kTrayCallbackMessage = WM_APP + 0x271;

        /// @brief Copies validated packed RGBA8 pixels into a native BGRA8 buffer.
        void copyRgbaToBgra(std::span<const std::byte> source, std::byte *destination) noexcept;

        /// @brief Creates a native icon from retained RGBA8 pixels.
        [[nodiscard]] HICON createIcon(const OwnedIconImage &image) noexcept;
        /// @brief Selects the retained icon nearest to the requested dimensions.
        [[nodiscard]] const OwnedIconImage &closestIcon(std::span<const OwnedIconImage> images, int desiredWidth, int desiredHeight) noexcept;
        /// @brief Converts bounded UTF-8 shell text to a native tooltip buffer.
        [[nodiscard]] IO::Types::Status convertTip(std::string_view text, std::array<wchar_t, 260> &output);
    } // namespace Win32
} // namespace GameWIP::Desktop::Platform
