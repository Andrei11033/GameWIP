/// @file runtime_diagnostics.cpp
/// @brief Structured window and operation-status diagnostics for the game runtime.

#include "runtime_diagnostics.h"

#if GAMEWIP_TRACY_ENABLED
#include "runtime/profiling.h"
#endif

#include "logger/logging.h"
#include "logger/message_format.h"
#include "logger/reports.h"
#include "terminal/output.h"

#include <format>
#include <string>
#include <string_view>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Terminal = GameWIP::Terminal;
    namespace WindowEvents = GameWIP::Desktop::Types::Events;
    using LogArea = GameWIP::Game::Logging::Area;

    /// ------------------------------------------------------------
    /// Window labels
    /// ------------------------------------------------------------

    /// @brief Returns a stable name for a top-level window mode.
    [[nodiscard]] constexpr std::string_view windowModeName(GameWIP::Desktop::Types::WindowMode mode) noexcept
    {
        switch(mode)
        {
        case GameWIP::Desktop::Types::WindowMode::Windowed:
            return "windowed";
        case GameWIP::Desktop::Types::WindowMode::BorderlessFullscreen:
            return "borderless_fullscreen";
        case GameWIP::Desktop::Types::WindowMode::ExclusiveFullscreen:
            return "exclusive_fullscreen";
        }
        return "unknown";
    }

    /// @brief Returns a stable name for a native presentation state.
    [[nodiscard]] constexpr std::string_view presentationStateName(GameWIP::Desktop::Types::PresentationState state) noexcept
    {
        switch(state)
        {
        case GameWIP::Desktop::Types::PresentationState::Normal:
            return "normal";
        case GameWIP::Desktop::Types::PresentationState::Minimized:
            return "minimized";
        case GameWIP::Desktop::Types::PresentationState::Maximized:
            return "maximized";
        }
        return "unknown";
    }

    /// @brief Returns a stable name for the source of a close request.
    [[nodiscard]] constexpr std::string_view closeRequestSourceName(GameWIP::Desktop::Types::Events::CloseRequestSource source) noexcept
    {
        using Source = GameWIP::Desktop::Types::Events::CloseRequestSource;
        switch(source)
        {
        case Source::User:
            return "user";
        case Source::Programmatic:
            return "programmatic";
        case Source::System:
            return "system";
        }
        return "unknown";
    }

    /// @brief Names every supported window-event kind for stable diagnostic fields.
    [[nodiscard]] constexpr std::string_view windowEventKindName(GameWIP::Desktop::Types::Event::Kind kind) noexcept
    {
        using Kind = GameWIP::Desktop::Types::Event::Kind;
        switch(kind)
        {
        case Kind::CloseRequested:
            return "close_requested";
        case Kind::NativeDestroyed:
            return "native_destroyed";
        case Kind::InteractiveMoveResizeStarted:
            return "interactive_move_resize_started";
        case Kind::InteractiveMoveResizeEnded:
            return "interactive_move_resize_ended";
        case Kind::VisibilityChanged:
            return "visibility_changed";
        case Kind::ClientPositionChanged:
            return "client_position_changed";
        case Kind::ClientSizeChanged:
            return "client_size_changed";
        case Kind::FramebufferSizeChanged:
            return "framebuffer_size_changed";
        case Kind::FocusChanged:
            return "focus_changed";
        case Kind::PresentationStateChanged:
            return "presentation_state_changed";
        case Kind::ContentScaleChanged:
            return "content_scale_changed";
        case Kind::MonitorChanged:
            return "monitor_changed";
        case Kind::WindowModeChanged:
            return "window_mode_changed";
        case Kind::OwnerChanged:
            return "owner_changed";
        case Kind::DisplayConfigurationChanged:
            return "display_configuration_changed";
        case Kind::CursorPresenceChanged:
            return "cursor_presence_changed";
        case Kind::FilesDropped:
            return "files_dropped";
        case Kind::OcclusionChanged:
            return "occlusion_changed";
        case Kind::RedrawRequested:
            return "redraw_requested";
        }
        return "unknown";
    }

} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
    /// ------------------------------------------------------------
    /// Operation status
    /// ------------------------------------------------------------

    void writeEmergencyMessage(std::string_view message) noexcept
    {
        static_cast<void>(Terminal::writeLine(Terminal::Types::Output::Stream::Stderr, message));
    }

    void writeEmergencyStatus(std::string_view action, const IO::Types::Status &status)
    {
        const std::string_view message = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        const std::string diagnostic =
            std::format("GameWIP: {}: code={}, nativeCode={}, message=\"{}\"", action, IO::errorCodeName(status.code), status.nativeCode, message);
        writeEmergencyMessage(diagnostic);
    }

    void reportFailure(Logging::Area area, std::string_view action, const IO::Types::Status &status)
    {
        const std::string_view statusMessage = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        const std::string_view errorCode = IO::errorCodeName(status.code);

#if GAMEWIP_TRACY_ENABLED
        const std::string message = std::format("{}: {} (code={}, nativeCode={})", action, statusMessage, errorCode, status.nativeCode);
        Profiling::message(message);
#endif

        const Logger::Types::Report::Result report = Logger::reportError(
            area,
            Logger::record()
                .field("action", action)
                .field("error_code", errorCode)
                .field("native_code", status.nativeCode)
                .field("status_message", statusMessage),
            "{}: \"{}\" (code={}, nativeCode={})",
            action,
            Logger::quoted(statusMessage),
            Logger::code(errorCode),
            Logger::number(status.nativeCode));

        if(!report.status.ok())
        {
            writeEmergencyStatus(action, status);
            writeEmergencyStatus("Runtime error report delivery failed", report.status);
        }
        else if(report.delivery == Logger::Types::Report::Delivery::None)
        {
            writeEmergencyStatus(action, status);
        }
    }

    void reportWarning(Logging::Area area, std::string_view action, const IO::Types::Status &status)
    {
        const std::string_view statusMessage = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        const std::string_view errorCode = IO::errorCodeName(status.code);
        Logger::warn(
            area,
            Logger::record()
                .field("action", action)
                .field("error_code", errorCode)
                .field("native_code", status.nativeCode)
                .field("status_message", statusMessage),
            "{}: \"{}\" (code={}, nativeCode={})",
            action,
            Logger::quoted(statusMessage),
            Logger::code(errorCode),
            Logger::number(status.nativeCode));
    }

    /// ------------------------------------------------------------
    /// Window event reports
    /// ------------------------------------------------------------

    void logWindowEvent(const Desktop::Types::Event &event, bool includeDetails)
    {
        using Kind = Desktop::Types::Event::Kind;

        const bool logDebug = Logger::shouldLog(Logger::Types::Level::Debug, LogArea::Window);
        const bool logTrace = Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Window);

        // Always report close requests; the switch controls other Window events.
        if(const auto *close = event.getIf<WindowEvents::CloseRequested>(); close != nullptr)
        {
            Logger::Record record = Logger::record();
            record.field("event_sequence", event.sequence)
                .field("event_kind", windowEventKindName(event.kind()))
                .field("close_source", closeRequestSourceName(close->source));
            Logger::info(
                LogArea::Window,
                record,
                "Window close requested by {} at event #{}",
                Logger::code(closeRequestSourceName(close->source)),
                Logger::number(event.sequence));
#if GAMEWIP_TRACY_ENABLED
            GameWIP::Profiling::message(std::format("Window close requested (source={})", closeRequestSourceName(close->source)));
#endif

            return;
        }

        if(!includeDetails)
        {
            return;
        }

        if(!logTrace && !logDebug)
        {
            return;
        }

        // Build event-specific fields, then emit one record at the selected level.
        Logger::Record record = Logger::record();
        record.field("event_sequence", event.sequence).field("event_kind", windowEventKindName(event.kind()));

        switch(event.kind())
        {
        case Kind::CloseRequested:
        case Kind::NativeDestroyed:
        case Kind::InteractiveMoveResizeStarted:
        case Kind::InteractiveMoveResizeEnded:
        case Kind::DisplayConfigurationChanged:
        case Kind::RedrawRequested:
            break;

        case Kind::VisibilityChanged:
            if(const auto *visibility = event.getIf<WindowEvents::VisibilityChanged>(); visibility != nullptr)
            {
                record.field("visible", visibility->visible);
            }
            break;

        case Kind::ClientPositionChanged:
            if(const auto *position = event.getIf<WindowEvents::ClientPositionChanged>(); position != nullptr)
            {
                record.field("x", position->position.x).field("y", position->position.y);
            }
            break;

        case Kind::ClientSizeChanged:
            if(const auto *clientSize = event.getIf<WindowEvents::ClientSizeChanged>(); clientSize != nullptr)
            {
                record.field("width", clientSize->size.width).field("height", clientSize->size.height);
            }
            break;

        case Kind::FramebufferSizeChanged:
            if(const auto *framebufferSize = event.getIf<WindowEvents::FramebufferSizeChanged>(); framebufferSize != nullptr)
            {
                record.field("width", framebufferSize->size.width).field("height", framebufferSize->size.height);
            }
            break;

        case Kind::FocusChanged:
            if(const auto *focus = event.getIf<WindowEvents::FocusChanged>(); focus != nullptr)
            {
                record.field("focused", focus->focused);
            }
            break;

        case Kind::PresentationStateChanged:
            if(const auto *state = event.getIf<WindowEvents::PresentationStateChanged>(); state != nullptr)
            {
                record.field("presentation_state", presentationStateName(state->state));
            }
            break;

        case Kind::ContentScaleChanged:
            if(const auto *scale = event.getIf<WindowEvents::ContentScaleChanged>(); scale != nullptr)
            {
                if(logTrace)
                {
                    record.field("previous_scale_x", scale->previousScale.x)
                        .field("previous_scale_y", scale->previousScale.y)
                        .field("scale_x", scale->scale.x)
                        .field("scale_y", scale->scale.y)
                        .field("previous_dpi_x", scale->previousDpi.x)
                        .field("previous_dpi_y", scale->previousDpi.y)
                        .field("dpi_x", scale->dpi.x)
                        .field("dpi_y", scale->dpi.y)
                        .field("framebuffer_width", scale->framebufferSize.width)
                        .field("framebuffer_height", scale->framebufferSize.height);
                }
                else
                {
                    record.field("scale_x", scale->scale.x).field("scale_y", scale->scale.y);
                }
            }
            break;

        case Kind::MonitorChanged:
            if(const auto *monitor = event.getIf<WindowEvents::MonitorChanged>(); monitor != nullptr)
            {
                record.field("previous_monitor_id", monitor->previousMonitor.value).field("monitor_id", monitor->monitor.value);
            }
            break;

        case Kind::WindowModeChanged:
            if(const auto *mode = event.getIf<WindowEvents::WindowModeChanged>(); mode != nullptr)
            {
                record.field("previous_mode", windowModeName(mode->previous)).field("window_mode", windowModeName(mode->current));
            }
            break;

        case Kind::OwnerChanged:
            if(const auto *owner = event.getIf<WindowEvents::OwnerChanged>(); owner != nullptr)
            {
                record.field("had_previous_owner", owner->previousOwner.has_value()).field("has_owner", owner->owner.has_value());

                if(owner->previousOwner.has_value())
                {
                    record.field("previous_owner_id", owner->previousOwner->value);
                }

                if(owner->owner.has_value())
                {
                    record.field("owner_id", owner->owner->value);
                }
            }
            break;

        case Kind::CursorPresenceChanged:
            if(const auto *cursor = event.getIf<WindowEvents::CursorPresenceChanged>(); cursor != nullptr)
            {
                record.field("inside_client_area", cursor->insideClientArea);
            }
            break;

        case Kind::FilesDropped:
            if(const auto *files = event.getIf<WindowEvents::FilesDropped>(); files != nullptr)
            {
                record.field("path_count", files->paths.size());
                if(files->clientPosition.has_value())
                {
                    record.field("x", files->clientPosition->x).field("y", files->clientPosition->y);
                }
            }
            break;

        case Kind::OcclusionChanged:
            if(const auto *occlusion = event.getIf<WindowEvents::OcclusionChanged>(); occlusion != nullptr)
            {
                record.field("occluded", occlusion->occluded);
            }
            break;
        }

        // Use one message shape for both detail levels.
        if(logTrace)
        {
            Logger::trace(
                LogArea::Window,
                record,
                "Window event {} #{}",
                Logger::code(windowEventKindName(event.kind())),
                Logger::number(event.sequence));
        }
        else
        {
            Logger::debug(
                LogArea::Window,
                record,
                "Window event {} #{}",
                Logger::code(windowEventKindName(event.kind())),
                Logger::number(event.sequence));
        }
    }

    /// ------------------------------------------------------------
    /// Window startup state
    /// ------------------------------------------------------------

    void logWindowSnapshot(const Desktop::Window &window)
    {
        Logger::info(LogArea::Window, "Window started");
        if(!Logger::shouldLog(Logger::Types::Level::Debug, LogArea::Window))
        {
            return;
        }

        const auto clientSize = window.clientSize();
        const auto framebufferSize = window.framebufferSize();
        const auto monitor = window.currentMonitor();
        const auto fullscreen = window.fullscreenState();
        Logger::Record record = Logger::record();
        record.field("visible", window.visible()).field("focused", window.focused()).field("fullscreen_suspended", fullscreen.suspended);
        record.field("title", window.title());
        if(monitor.isValid())
        {
            Logger::debug(
                LogArea::Window,
                record,
                "Window state: {} on monitor #{} (client {}x{}, framebuffer {}x{})",
                windowModeName(window.windowMode()),
                Logger::number(monitor.value),
                Logger::number(clientSize.width),
                Logger::number(clientSize.height),
                Logger::number(framebufferSize.width),
                Logger::number(framebufferSize.height));
        }
        else
        {
            Logger::debug(
                LogArea::Window,
                record,
                "Window state: {} (client {}x{}, framebuffer {}x{})",
                windowModeName(window.windowMode()),
                Logger::number(clientSize.width),
                Logger::number(clientSize.height),
                Logger::number(framebufferSize.width),
                Logger::number(framebufferSize.height));
        }
    }

} // namespace GameWIP::Game::RuntimeDiagnostics
