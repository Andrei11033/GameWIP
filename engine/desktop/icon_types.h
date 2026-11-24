/// @file icon_types.h
/// @brief Shared portable Desktop icon-image values.

#pragma once

#include "desktop/types.h"

#include <cstddef>
#include <span>

/// ------------------------------------------------------------
/// @brief Borrowed icon image data shared by Window and shell APIs.
/// ------------------------------------------------------------
namespace GameWIP::Desktop::Types
{
    /// @brief Call-scoped tightly packed RGBA8 icon image.
    struct IconImageView
    {
        PixelSize size;                   ///< Physical icon dimensions.
        std::span<const std::byte> rgba8; ///< Tightly packed row-major RGBA8 pixels.
    };
} // namespace GameWIP::Desktop::Types
