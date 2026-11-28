/// @file geometry.h
/// @brief Portable logical geometry types.

#pragma once

#include <cstdint>
#include <optional>

namespace GameWIP::Desktop::Types::Accessibility
{
    /// ------------------------------------------------------------
    /// Logical geometry
    /// ------------------------------------------------------------

    /// @brief Floating-point point in Window client logical coordinates.
    struct Point
    {
        double x = 0.0; ///< Horizontal coordinate in the operation's documented logical space.
        double y = 0.0; ///< Vertical coordinate in the operation's documented logical space.
    };

    /// @brief Floating-point rectangle in a node's local logical coordinates.
    struct Rect
    {
        double x = 0.0;      ///< Left edge in the containing field's documented logical space.
        double y = 0.0;      ///< Top edge in the containing field's documented logical space.
        double width = 0.0;  ///< Finite nonnegative logical width.
        double height = 0.0; ///< Finite nonnegative logical height.
    };

    /// @brief Two-dimensional affine transform from node-local coordinates to Window client coordinates.
    struct AffineTransform
    {
        double m11 = 1.0; ///< Local x contribution to Window x.
        double m12 = 0.0; ///< Local x contribution to Window y.
        double m21 = 0.0; ///< Local y contribution to Window x.
        double m22 = 1.0; ///< Local y contribution to Window y.
        double tx = 0.0;  ///< Window-client logical x translation.
        double ty = 0.0;  ///< Window-client logical y translation.
    };

    /// @brief Optional node geometry and hit-testing metadata.
    struct Geometry
    {
        Rect localBounds;                   ///< Rectangle in this node's local logical coordinates.
        AffineTransform toWindow;           ///< Direct local-to-Window transform, not relative to the parent.
        std::optional<Rect> clippingBounds; ///< Window-client logical clip inherited by descendants.
        bool visible = true;                ///< Combines with ancestor and Window visibility for native exposure.
        bool hitTestable = true;            ///< Requires an invertible transform when true.
        std::uint32_t hitTestOrder = 0;     ///< Higher orders win overlap; ties use later semantic preorder.
    };
} // namespace GameWIP::Desktop::Types::Accessibility
