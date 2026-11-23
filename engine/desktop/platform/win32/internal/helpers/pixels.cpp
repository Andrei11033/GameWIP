/// @file pixels.cpp
/// @brief Pixel-format conversion for Desktop's Win32 backend.

#include "desktop/platform/win32/internal/win32_pixel_helpers.h"

namespace GameWIP::Desktop::Platform::Win32
{
    bool copyRgbaToBgra(std::span<const std::byte> source, std::span<std::byte> destination) noexcept
    {
        if(source.size() != destination.size() || source.size() % 4 != 0)
        {
            return false;
        }

        for(std::size_t offset = 0; offset < source.size(); offset += 4)
        {
            destination[offset] = source[offset + 2];
            destination[offset + 1] = source[offset + 1];
            destination[offset + 2] = source[offset];
            destination[offset + 3] = source[offset + 3];
        }

        return true;
    }
} // namespace GameWIP::Desktop::Platform::Win32
