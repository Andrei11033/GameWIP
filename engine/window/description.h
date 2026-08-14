/// @file description.h
/// @brief Window creation policy and configuration vocabulary.

#pragma once

#include "window/display.h"
#include "window/types.h"

#include <optional>
#include <string>

namespace GameWIP::Window::Types
{
    /// @brief Top-level Window mode.
    enum class Mode
    {
        Windowed,
        BorderlessFullscreen,
        ExclusiveFullscreen
    };

    /// @brief Non-client decoration policy.
    enum class DecorationMode
    {
        System,
        Borderless,
        Custom
    };

    /// @brief Initial desktop placement policy.
    enum class PlacementKind
    {
        PlatformDefault,
        Centered,
        Explicit
    };

    /// @brief Initial placement request.
    struct Placement
    {
        PlacementKind kind = PlacementKind::PlatformDefault;
        Display::MonitorId monitor;
        ScreenPosition position;
    };

    /// @brief Requested top-level mode and optional exclusive display mode.
    struct ModeRequest
    {
        Mode mode = Mode::Windowed;
        Display::MonitorId monitor;
        std::optional<Display::Mode> displayMode;
    };

    /// @brief Availability of standard system Window controls.
    struct Controls
    {
        bool closable = true;
        bool minimizable = true;
        bool maximizable = true;
        friend constexpr bool operator==(Controls, Controls) noexcept = default;
    };

    /// @brief Behavior applied when the effective monitor DPI changes.
    enum class DpiResizePolicy
    {
        PreserveLogicalClientSize,
        PreservePhysicalClientSize
    };

    /// @brief Window-owned cursor policy.
    enum class CursorMode
    {
        Normal,
        Hidden,
        Confined,
        HiddenConfined,
        Relative
    };

    /// @brief Standard system cursor shape.
    enum class CursorShape
    {
        Arrow,
        Text,
        Crosshair,
        Hand,
        Help,
        Wait,
        Progress,
        Move,
        ResizeAll,
        ResizeHorizontal,
        ResizeVertical,
        ResizeDiagonalNorthWestSouthEast,
        ResizeDiagonalNorthEastSouthWest,
        NotAllowed
    };

    /// @brief Pointer hit-test policy.
    enum class PointerInputMode
    {
        Normal,
        ClickThrough,
        AcceptRegions,
        IgnoreRegions,
        HitMask
    };

    /// @brief Optional platform backdrop treatment.
    enum class BackdropEffect
    {
        None,
        Automatic,
        MainWindow,
        TransientWindow,
        TabbedWindow
    };

    /// @brief Complete initial top-level Window description.
    struct Description
    {
        std::string title = "GameWIP"; ///< UTF-8; embedded U+0000 is rejected by native title operations.
        LogicalSize clientSize{1280, 720};
        DpiResizePolicy dpiResizePolicy = DpiResizePolicy::PreserveLogicalClientSize;
        Placement placement;
        ModeRequest mode;
        PresentationState presentation = PresentationState::Normal;
        DecorationMode decoration = DecorationMode::System;
        Controls controls;
        SizeLimits sizeLimits;
        std::optional<AspectRatio> aspectRatio;
        WindowId owner;
        CursorMode cursorMode = CursorMode::Normal;
        CursorShape cursorShape = CursorShape::Arrow;
        PointerInputMode pointerInputMode = PointerInputMode::Normal;
        BackdropEffect backdropEffect = BackdropEffect::None;
        float opacity = 1.0F;
        bool visible = false;
        bool requestFocus = false;
        bool resizable = true;
        bool focusable = true;
        bool userInteractionEnabled = true;
        bool alwaysOnTop = false;
        bool fileDropEnabled = false;
        bool transparentFramebuffer = false;
    };
} // namespace GameWIP::Window::Types
