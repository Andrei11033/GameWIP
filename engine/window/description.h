/// @file description.h
/// @brief Window creation policy and configuration vocabulary.

#pragma once

#include "window/display.h"
#include "window/types.h"

#include <optional>
#include <string>

namespace GameWIP::Window::Types
{
    /// @brief Non-client decoration policy.
    enum class DecorationMode
    {
        System,     ///< Platform supplies the frame, caption, and standard hit testing.
        Borderless, ///< No non-client frame or built-in custom hit regions.
        Custom      ///< Client supplies declarative drag and caption-control regions.
    };

    /// @brief Initial desktop placement policy.
    enum class PlacementKind
    {
        PlatformDefault, ///< Let the platform choose the initial desktop position.
        Centered,        ///< Center the Window on the requested or primary monitor.
        Explicit         ///< Use Placement::position as a physical virtual-screen client origin.
    };

    /// @brief Initial placement request.
    struct Placement
    {
        PlacementKind kind = PlacementKind::PlatformDefault; ///< Placement strategy.
        Display::MonitorId monitor;                          ///< Target monitor when applicable.
        ScreenPosition position;                             ///< Explicit position when requested.
    };

    /// @brief Requested top-level mode and optional exclusive display mode.
    struct ModeRequest
    {
        Mode mode = Mode::Windowed;               ///< Requested top-level mode.
        Display::MonitorId monitor;               ///< Target fullscreen monitor.
        std::optional<Display::Mode> displayMode; ///< Optional exclusive physical mode.
    };

    /// @brief Availability of standard system Window controls.
    struct Controls
    {
        bool closable = true;    ///< Whether the close control is enabled.
        bool minimizable = true; ///< Whether the minimize control is enabled.
        bool maximizable = true; ///< Whether the maximize control is enabled.
        /// @brief Compares every control flag.
        friend constexpr bool operator==(Controls, Controls) noexcept = default;
    };

    /// @brief Behavior applied when the effective monitor DPI changes.
    enum class DpiResizePolicy
    {
        PreserveLogicalClientSize, ///< Keep the logical client extent and resize the framebuffer.
        PreservePhysicalClientSize ///< Keep the physical client pixels and change the logical extent.
    };

    /// @brief Window-owned cursor policy.
    enum class CursorMode
    {
        Normal,         ///< Visible, unconstrained system cursor.
        Hidden,         ///< Hidden cursor without confinement.
        Confined,       ///< Visible cursor confined to the client area.
        HiddenConfined, ///< Hidden cursor confined to the client area.
        Relative        ///< Hidden, confined cursor producing relative-style motion around a stable anchor.
    };

    /// @brief Standard system cursor shape.
    enum class CursorShape
    {
        Arrow,                            ///< Normal arrow pointer.
        Text,                             ///< Text-selection I-beam.
        Crosshair,                        ///< Precision crosshair.
        Hand,                             ///< Link or pointing hand.
        Help,                             ///< Help-selection pointer.
        Wait,                             ///< Busy pointer that does not imply interaction remains available.
        Progress,                         ///< Background-progress pointer.
        Move,                             ///< Four-way move pointer.
        ResizeAll,                        ///< Four-way resize pointer.
        ResizeHorizontal,                 ///< Left/right resize pointer.
        ResizeVertical,                   ///< Up/down resize pointer.
        ResizeDiagonalNorthWestSouthEast, ///< Northwest/southeast diagonal resize pointer.
        ResizeDiagonalNorthEastSouthWest, ///< Northeast/southwest diagonal resize pointer.
        NotAllowed                        ///< Operation-not-allowed pointer.
    };

    /// @brief Pointer hit-test policy.
    enum class PointerInputMode
    {
        Normal,        ///< Accept pointer input across the complete client area.
        ClickThrough,  ///< Ignore pointer input across the complete client area.
        AcceptRegions, ///< Accept only points inside one of the copied logical regions.
        IgnoreRegions, ///< Ignore points inside copied regions and accept the remainder.
        HitMask        ///< Use the renderer-published framebuffer-pixel mask.
    };

    /// @brief Optional platform backdrop treatment.
    enum class BackdropEffect
    {
        None,            ///< Disable optional system backdrop treatment.
        Automatic,       ///< Let the platform choose a suitable backdrop.
        MainWindow,      ///< Request the platform treatment intended for a long-lived main Window.
        TransientWindow, ///< Request the platform treatment intended for transient surfaces.
        TabbedWindow     ///< Request the platform treatment intended for a tabbed title area.
    };

    /// @brief Complete initial top-level Window description.
    struct Description
    {
        std::string title = "GameWIP";     ///< UTF-8; embedded U+0000 is rejected by native title operations.
        LogicalSize clientSize{1280, 720}; ///< Initial logical client extent.
        DpiResizePolicy dpiResizePolicy = DpiResizePolicy::PreserveLogicalClientSize; ///< DPI resize behavior.
        Placement placement;                                                          ///< Initial desktop placement.
        ModeRequest mode;                                                             ///< Initial top-level mode.
        PresentationState presentation = PresentationState::Normal;                   ///< Initial presentation state.
        DecorationMode decoration = DecorationMode::System;                           ///< Initial decoration policy.
        Controls controls;                                                            ///< Initial standard-control availability.
        SizeLimits sizeLimits;                                                        ///< Initial logical size limits.
        std::optional<AspectRatio> aspectRatio;                                       ///< Optional client aspect-ratio constraint.
        WindowId owner;                                                               ///< Optional owner Window identity.
        CursorMode cursorMode = CursorMode::Normal;                                   ///< Initial cursor mode.
        CursorShape cursorShape = CursorShape::Arrow;                                 ///< Initial cursor shape.
        PointerInputMode pointerInputMode = PointerInputMode::Normal;                 ///< Initial pointer hit-test policy.
        BackdropEffect backdropEffect = BackdropEffect::None;                         ///< Initial backdrop treatment.
        float opacity = 1.0F;                                                         ///< Initial opacity in the inclusive range [0, 1].
        bool visible = false;                                                         ///< Whether to show after opening.
        bool requestFocus = false;                                                    ///< Whether to request activation after opening.
        bool resizable = true;                                                        ///< Whether user resizing is enabled.
        bool focusable = true;                                                        ///< Whether the Window can receive focus.
        bool userInteractionEnabled = true;                                           ///< Whether user interaction is enabled.
        bool alwaysOnTop = false;                                                     ///< Whether topmost ordering is requested.
        bool fileDropEnabled = false;                                                 ///< Whether portable file-drop events are enabled.
        bool transparentFramebuffer = false;                                          ///< Whether framebuffer alpha reaches the desktop.
    };
} // namespace GameWIP::Window::Types
