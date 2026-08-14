/// @file events.h
/// @brief Portable Window event payloads, fixed-queue metadata, and event pumping.

#pragma once

#include "filesystem/filesystem.h"
#include "io/io.h"
#include "window/description.h"
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
    enum class CloseRequestSource
    {
        User,
        Programmatic,
        System
    };

    struct CloseRequested
    {
        CloseRequestSource source = CloseRequestSource::User;
    };
    struct NativeDestroyed
    {
    };
    struct VisibilityChanged
    {
        bool visible = false;
    };
    struct ClientPositionChanged
    {
        ScreenPosition position;
    };
    struct ClientSizeChanged
    {
        LogicalSize size;
    };
    struct FramebufferSizeChanged
    {
        PixelSize size;
    };
    struct FocusChanged
    {
        bool focused = false;
    };
    struct PresentationStateChanged
    {
        PresentationState state = PresentationState::Normal;
    };
    struct ContentScaleChanged
    {
        ContentScale previousScale;
        ContentScale scale;
        Dpi previousDpi;
        Dpi dpi;
        PixelSize framebufferSize;
    };
    struct MonitorChanged
    {
        Display::MonitorId previousMonitor;
        Display::MonitorId monitor;
    };
    struct ModeChanged
    {
        Mode previousMode = Mode::Windowed;
        Mode mode = Mode::Windowed;
    };
    struct OwnerChanged
    {
        WindowId previousOwner;
        WindowId owner;
    };
    struct DisplayConfigurationChanged
    {
    };
    struct CursorPresenceChanged
    {
        bool insideClientArea = false;
    };
    struct FilesDropped
    {
        std::optional<LogicalPosition> clientPosition;
        std::vector<FileSystem::Types::Path> paths;
    };
    struct OcclusionChanged
    {
        bool occluded = false;
    };
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

    enum class StorageKind
    {
        Internal,
        External
    };

    struct QueueInfo
    {
        StorageKind storage = StorageKind::Internal;
        std::size_t capacity = 0;
        std::size_t pendingEvents = 0;
        std::uint64_t droppedEvents = 0;
    };

    struct PumpResult
    {
        IO::Types::Status status;
        std::size_t eventsQueued = 0;
        std::uint64_t eventsDropped = 0;
        bool timedOut = false;
    };
} // namespace GameWIP::Window::Types::Events

namespace GameWIP::Window::Types
{
    /// @brief One queued event with a per-open monotonic sequence.
    struct Event
    {
        std::uint64_t sequence = 0;
        Events::Payload data;

        template <typename PayloadType> [[nodiscard]] PayloadType *getIf() noexcept
        {
            return std::get_if<PayloadType>(&data);
        }

        template <typename PayloadType> [[nodiscard]] const PayloadType *getIf() const noexcept
        {
            return std::get_if<PayloadType>(&data);
        }
    };
} // namespace GameWIP::Window::Types

/// @brief Calling-thread Window event-pump operations and queue defaults.
namespace GameWIP::Window::Events
{
    inline constexpr std::size_t kDefaultQueueCapacity = 256;
    inline constexpr std::chrono::milliseconds kNoWait{0};
    inline constexpr std::chrono::milliseconds kWaitForever{-1};

    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Events::PumpResult poll() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Events::PumpResult wait(std::chrono::milliseconds timeout = kWaitForever) noexcept;
} // namespace GameWIP::Window::Events
