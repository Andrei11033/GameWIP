/// @file events.h
/// @brief Portable Window event payloads, fixed-queue metadata, and event pumping.

#pragma once

#include "filesystem/filesystem.h"
#include "io/io.h"
#include "window/display.h"
#include "window/window_export.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace GameWIP::Window::Types::Events
{
    /// @brief Origin of a close request.
    enum class CloseRequestSource
    {
        User,
        Programmatic,
        System
    };

    /// @brief Signals that closing was requested without destroying the native Window.
    struct CloseRequested
    {
        CloseRequestSource source = CloseRequestSource::User; ///< Request origin.
    };
    /// @brief Signals that the native Window was destroyed externally.
    struct NativeDestroyed
    {
    };
    /// @brief Reports a visibility transition.
    struct VisibilityChanged
    {
        bool visible = false; ///< New visibility state.
    };
    /// @brief Reports a client-area position transition.
    struct ClientPositionChanged
    {
        ScreenPosition position; ///< New virtual-screen position.
    };
    /// @brief Reports a logical client-size transition.
    struct ClientSizeChanged
    {
        LogicalSize size; ///< New logical client extent.
    };
    /// @brief Reports a physical framebuffer-size transition.
    struct FramebufferSizeChanged
    {
        PixelSize size; ///< New physical framebuffer extent.
    };
    /// @brief Reports a keyboard-focus transition.
    struct FocusChanged
    {
        bool focused = false; ///< New focus state.
    };
    /// @brief Reports a presentation-state transition.
    struct PresentationStateChanged
    {
        PresentationState state = PresentationState::Normal; ///< New presentation state.
    };
    /// @brief Reports content-scale, DPI, and framebuffer changes as one transition.
    struct ContentScaleChanged
    {
        ContentScale previousScale; ///< Scale before the transition.
        ContentScale scale;         ///< Scale after the transition.
        Dpi previousDpi;            ///< Effective DPI before the transition.
        Dpi dpi;                    ///< Effective DPI after the transition.
        PixelSize framebufferSize;  ///< Resulting physical framebuffer extent.
    };
    /// @brief Reports a current-monitor transition.
    struct MonitorChanged
    {
        Display::MonitorId previousMonitor; ///< Monitor before the transition.
        Display::MonitorId monitor;         ///< Monitor after the transition.
    };
    /// @brief Reports a top-level mode transition.
    struct ModeChanged
    {
        Mode previousMode = Mode::Windowed; ///< Mode before the transition.
        Mode mode = Mode::Windowed;         ///< Mode after the transition.
    };
    /// @brief Reports an owner identity transition.
    struct OwnerChanged
    {
        WindowId previousOwner; ///< Owner before the transition.
        WindowId owner;         ///< Owner after the transition.
    };
    /// @brief Signals that the desktop display configuration changed.
    struct DisplayConfigurationChanged
    {
    };
    /// @brief Reports whether the cursor crossed the client-area boundary.
    struct CursorPresenceChanged
    {
        bool insideClientArea = false; ///< Whether the cursor is now inside the client area.
    };
    /// @brief Delivers accepted file paths dropped onto the Window.
    struct FilesDropped
    {
        std::optional<LogicalPosition> clientPosition; ///< Drop position when supplied by the platform.
        std::vector<FileSystem::Types::Path> paths;    ///< Materialized native file-system paths.
    };
    /// @brief Reports renderer-supplied occlusion state.
    struct OcclusionChanged
    {
        bool occluded = false; ///< New occlusion state.
    };
    /// @brief Requests that the client redraw the Window.
    struct RedrawRequested
    {
    };

    /// @brief Tagged payload for one portable Window event.
    using Payload = std::variant<
        CloseRequested,
        NativeDestroyed,
        VisibilityChanged,
        ClientPositionChanged,
        ClientSizeChanged,
        FramebufferSizeChanged,
        FocusChanged,
        PresentationStateChanged,
        ContentScaleChanged,
        MonitorChanged,
        ModeChanged,
        OwnerChanged,
        DisplayConfigurationChanged,
        CursorPresenceChanged,
        FilesDropped,
        OcclusionChanged,
        RedrawRequested>;

    /// @brief Ownership model for a Window event queue's storage.
    enum class StorageKind
    {
        Internal,
        External
    };

    /// @brief Snapshot of event-queue storage and counters.
    struct QueueInfo
    {
        StorageKind storage = StorageKind::Internal; ///< Storage ownership model.
        std::size_t capacity = 0;                    ///< Maximum queued-event count.
        std::size_t pendingEvents = 0;               ///< Events waiting to be consumed.
        std::uint64_t droppedEvents = 0;             ///< Events dropped during this open lifetime.
    };

    /// @brief Result of one calling-thread event-pump operation.
    struct PumpResult
    {
        IO::Types::Status status;        ///< Pump status.
        std::size_t eventsQueued = 0;    ///< Events newly queued during this pump.
        std::uint64_t eventsDropped = 0; ///< Events dropped during this pump.
        bool timedOut = false;           ///< Whether a finite wait expired without native work.
    };
} // namespace GameWIP::Window::Types::Events

namespace GameWIP::Window::Types
{
    /// @brief One queued event with a per-open monotonic sequence.
    struct Event
    {
        std::uint64_t sequence = 0; ///< Monotonic sequence within the current open lifetime.
        Events::Payload data;       ///< Tagged event payload.

        /// @brief Returns the mutable payload when it has the requested type.
        template <typename PayloadType> [[nodiscard]] PayloadType *getIf() noexcept
        {
            return std::get_if<PayloadType>(&data);
        }

        /// @brief Returns the immutable payload when it has the requested type.
        template <typename PayloadType> [[nodiscard]] const PayloadType *getIf() const noexcept
        {
            return std::get_if<PayloadType>(&data);
        }
    };
} // namespace GameWIP::Window::Types

/// @brief Calling-thread Window event-pump operations and queue defaults.
namespace GameWIP::Window::Events
{
    inline constexpr std::size_t kDefaultQueueCapacity = 256;    ///< Default internal event slots.
    inline constexpr std::chrono::milliseconds kNoWait{0};       ///< Non-blocking wait duration.
    inline constexpr std::chrono::milliseconds kWaitForever{-1}; ///< Unbounded wait sentinel.

    /// @brief Pumps currently pending native events for the calling thread without waiting.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Events::PumpResult poll() noexcept;
    /// @brief Waits for native work, then pumps events for the calling thread.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Events::PumpResult wait(std::chrono::milliseconds timeout = kWaitForever) noexcept;
} // namespace GameWIP::Window::Events
