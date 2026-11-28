/// @file win32_pixel_helpers.h
/// @brief Private Win32 pixel-format conversion helpers.

#pragma once

#include <cstddef>
#include <span>

namespace GameWIP::Desktop::Platform::Win32
{
    /// @brief Converts tightly packed RGBA8 pixels to BGRA8 pixels.
    [[nodiscard]] bool copyRgbaToBgra(std::span<const std::byte> source, std::span<std::byte> destination) noexcept;
} // namespace GameWIP::Desktop::Platform::Win32
