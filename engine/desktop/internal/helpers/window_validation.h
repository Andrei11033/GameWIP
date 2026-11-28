/// @file window_validation.h
/// @brief Shared private Window validation and ownership checks.

#pragma once

#include "desktop/window.h"

#include <optional>

namespace GameWIP::Desktop::Internal
{
    struct WindowState;
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop::Internal::Helpers
{
    /// ------------------------------------------------------------
    /// Window request validation
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status error(GameWIP::Status::Types::Code code) noexcept;
    [[nodiscard]] bool validSize(Types::LogicalSize size) noexcept;
    [[nodiscard]] bool validPixelSize(Types::PixelSize size) noexcept;
    [[nodiscard]] bool validRect(const Types::LogicalRect &rect) noexcept;
    [[nodiscard]] bool validLimits(const Types::SizeLimits &limits) noexcept;
    [[nodiscard]] bool sizeWithin(Types::LogicalSize size, const Types::SizeLimits &limits) noexcept;
    [[nodiscard]] bool validRatio(const std::optional<Types::AspectRatio> &ratio) noexcept;

    [[nodiscard]] bool validEnum(Types::PresentationState value) noexcept;
    [[nodiscard]] bool validEnum(Types::DecorationMode value) noexcept;
    [[nodiscard]] bool validEnum(Types::PlacementKind value) noexcept;
    [[nodiscard]] bool validEnum(Types::CursorMode value) noexcept;
    [[nodiscard]] bool validEnum(Types::CursorShape value) noexcept;
    [[nodiscard]] bool validEnum(Types::PointerInputMode value) noexcept;
    [[nodiscard]] bool validEnum(Types::BackdropEffect value) noexcept;
    [[nodiscard]] bool validEnum(Types::DpiResizePolicy value) noexcept;

    [[nodiscard]] bool validDisplayMode(const Types::Display::Mode &mode) noexcept;
    [[nodiscard]] bool validModeRequest(const Types::WindowModeRequest &request) noexcept;

    /// ------------------------------------------------------------
    /// Window access validation
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status requireState(WindowState *state) noexcept;

} // namespace GameWIP::Desktop::Internal::Helpers
