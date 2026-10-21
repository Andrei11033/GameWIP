/// @file description.h
/// @brief Window creation policy and configuration vocabulary.

#pragma once

#include "desktop/display.h"
#include "desktop/types.h"

#include <optional>
#include <string>
#include <variant>

namespace GameWIP::Desktop::Types
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
    /// @details A monitor may be supplied only for Centered placement; position is used only for Explicit placement.
    struct Placement
    {
        PlacementKind kind = PlacementKind::PlatformDefault; ///< Placement strategy.
        std::optional<Display::MonitorId> monitor; ///< Target monitor for Centered placement; nullopt selects the primary monitor. Ignored otherwise.
        ScreenPosition position;                   ///< Explicit position when requested.
    };

    /// @brief Initial/restored logical windowed geometry.
    /// @details This geometry is independent from any physical monitor mode requested by fullscreen.
    struct WindowedGeometry
    {
        LogicalSize clientSize{1280, 720}; ///< Initial and restored logical client extent; must satisfy ResizePolicy::limits.
        Placement placement;               ///< Initial desktop placement used for the windowed state.
    };

    /// @brief Requests a normal windowed presentation.
    struct WindowedModeRequest
    {
    };

    /// @brief Requests borderless fullscreen, optionally on a specific monitor.
    struct BorderlessFullscreenRequest
    {
        std::optional<Display::MonitorId> targetMonitor; ///< Nullopt chooses the current/nearest monitor.
    };

    /// @brief Requests exclusive fullscreen, optionally selecting a monitor and physical mode.
    struct ExclusiveFullscreenRequest
    {
        std::optional<Display::MonitorId> targetMonitor; ///< Nullopt chooses the current/nearest monitor.
        std::optional<Display::Mode> displayMode;        ///< Nullopt keeps the selected monitor's current physical mode.
    };

    /// @brief Type-safe request for a Window presentation mode; build it with WindowModes.
    using WindowModeRequest = std::variant<WindowedModeRequest, BorderlessFullscreenRequest, ExclusiveFullscreenRequest>;

    /// @brief Named constructors for the valid WindowModeRequest combinations.
    /// @details Omitted monitor means the current/nearest monitor; omitted display mode keeps that
    /// monitor's current physical mode. Fullscreen requests never change Description::windowed,
    /// which is only the restore size.
    namespace WindowModes
    {
        /// @brief Normal windowed presentation using Description::windowed.
        [[nodiscard]] constexpr WindowModeRequest windowed() noexcept
        {
            return WindowedModeRequest{};
        }

        /// @brief Borderless fullscreen on the current/nearest monitor; the physical display mode is unchanged.
        [[nodiscard]] constexpr WindowModeRequest borderlessFullscreen() noexcept
        {
            return BorderlessFullscreenRequest{};
        }

        /// @brief Borderless fullscreen on @p monitor; the physical display mode is unchanged.
        [[nodiscard]] constexpr WindowModeRequest borderlessFullscreen(Display::MonitorId monitor) noexcept
        {
            return BorderlessFullscreenRequest{monitor};
        }

        /// @brief Exclusive fullscreen on the current/nearest monitor, keeping its current physical mode.
        [[nodiscard]] constexpr WindowModeRequest exclusiveFullscreen() noexcept
        {
            return ExclusiveFullscreenRequest{};
        }

        /// @brief Exclusive fullscreen on the current/nearest monitor, switching it to @p displayMode.
        /// @details The monitor's previous mode is restored when fullscreen ends.
        [[nodiscard]] constexpr WindowModeRequest exclusiveFullscreen(Display::Mode displayMode) noexcept
        {
            return ExclusiveFullscreenRequest{std::nullopt, displayMode};
        }

        /// @brief Exclusive fullscreen on @p monitor, switching it to @p displayMode.
        [[nodiscard]] constexpr WindowModeRequest exclusiveFullscreen(Display::MonitorId monitor, Display::Mode displayMode) noexcept
        {
            return ExclusiveFullscreenRequest{monitor, displayMode};
        }
    } // namespace WindowModes

    /// @brief Startup visibility, focus, and activation policy.
    struct ActivationPolicy
    {
        bool visible = false;      ///< Whether the native Window is shown after opening; required by requestFocus.
        bool focusable = true;     ///< Whether the Window may receive keyboard focus; required by requestFocus.
        bool requestFocus = false; ///< Requests activation; requires visible=true and focusable=true.
    };

    /// @brief Availability of standard system Window controls.
    struct Controls
    {
        bool closable = true;    ///< Whether the close control is enabled.
        bool minimizable = true; ///< Whether the minimize control is enabled.
        bool maximizable = true; ///< Whether the maximize control is enabled.
        friend constexpr bool operator==(Controls, Controls) noexcept = default;
    };

    /// @brief User-resizing policy and constraints.
    struct ResizePolicy
    {
        bool enabled = true;                    ///< Whether user resizing is enabled; false requires controls.maximizable=false.
        Controls controls;                      ///< Standard controls; controls.maximizable requires enabled=true.
        SizeLimits limits;                      ///< Initial logical client-size limits.
        std::optional<AspectRatio> aspectRatio; ///< Optional logical client aspect-ratio constraint.
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
    /// @details requestFocus requires activation.visible and activation.focusable. A hidden Window
    /// must use Normal presentation. resizing.enabled=false requires resizing.controls.maximizable=false.
    /// windowed.clientSize must satisfy resizing.limits. Region and HitMask pointer modes require
    /// runtime layout/publication and cannot be selected at open.
    struct Description
    {
        std::string title = "GameWIP"; ///< UTF-8; embedded U+0000 is rejected by native title operations.
        WindowedGeometry windowed;     ///< Initial/restored logical windowed geometry; independent of physical display mode.
        DpiResizePolicy dpiResizePolicy = DpiResizePolicy::PreserveLogicalClientSize; ///< DPI resize behavior.
        WindowModeRequest initialWindowMode = WindowModes::windowed(); ///< Initial presentation request; separate from windowed geometry.
        PresentationState presentation = PresentationState::Normal;    ///< Initial presentation state.
        DecorationMode decoration = DecorationMode::System;            ///< Initial decoration policy.
        ActivationPolicy activation;                                   ///< Visibility and focus policy; contradictory combinations are rejected.
        ResizePolicy resizing;                      ///< Resizing, controls, limits, and aspect-ratio policy; contradictory combinations are rejected.
        std::optional<WindowId> owner;              ///< Optional owner Window identity; nullopt means no owner.
        CursorMode cursorMode = CursorMode::Normal; ///< Initial cursor mode.
        CursorShape cursorShape = CursorShape::Arrow;                 ///< Initial cursor shape.
        PointerInputMode pointerInputMode = PointerInputMode::Normal; ///< Initial pointer hit-test policy.
        BackdropEffect backdropEffect = BackdropEffect::None;         ///< Initial backdrop treatment.
        float opacity = 1.0F;                                         ///< Initial opacity in [0, 1].
        bool userInteractionEnabled = true;                           ///< Whether user interaction is enabled.
        bool alwaysOnTop = false;                                     ///< Whether topmost ordering is requested.
        bool fileDropEnabled = false;                                 ///< Whether portable file-drop events are enabled.
        bool transparentFramebuffer = false;                          ///< Whether framebuffer alpha reaches the desktop.
    };
} // namespace GameWIP::Desktop::Types
