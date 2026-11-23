/// @file runtime_diagnostics.h
/// @brief Internal structured diagnostic helpers used by the executable runtime.

#pragma once

#include "desktop/events.h"
#include "desktop/window.h"
#include "input/system.h"
#include "io/status.h"
#include "runtime/logging.h"

#include <cstdint>
#include <string_view>

/// @brief Runtime-owned status and state reporting, separate from runtime sequencing.
namespace GameWIP::Game::RuntimeDiagnostics
{
    namespace Desktop = GameWIP::Desktop;
    namespace IO = GameWIP::IO;
    namespace Input = GameWIP::Input;

    /// ------------------------------------------------------------
    /// Operation status
    /// ------------------------------------------------------------

    /// @brief Writes a final diagnostic without relying on Logger availability.
    void writeEmergencyMessage(std::string_view message) noexcept;

    /// @brief Writes a failed status with its portable and native codes to stderr.
    void writeEmergencyStatus(std::string_view action, const IO::Types::Status &status);

    /// @brief Synchronously reports a failed operation with its portable and native status.
    void reportFailure(Logging::Area area, std::string_view action, const IO::Types::Status &status);

    /// @brief Queues a recoverable operation issue with its portable and native status.
    void reportWarning(Logging::Area area, std::string_view action, const IO::Types::Status &status);

    /// ------------------------------------------------------------
    /// Window reports
    /// ------------------------------------------------------------

    /// @brief Logs one queued Window event when event details are enabled.
    void logWindowEvent(const Desktop::Types::Event &event, bool includeDetails);

    /// @brief Logs Window startup and, at Debug, its cached state and drawable extents.
    void logWindowSnapshot(const Desktop::Window &window);

    /// ------------------------------------------------------------
    /// Input inventory and events
    /// ------------------------------------------------------------

    /// @brief Logs concise device summaries and optional bounded descriptors.
    void logInputDevices(const Input::System &input, bool includeTraceDetails);

    /// @brief Logs one bounded grouped device descriptor and optional device event at Trace.
    void logInputDeviceTrace(
        const Input::Types::DeviceInfo *device,
        Input::Types::DeviceId id,
        const Input::Types::Event *event = nullptr,
        std::uint64_t captureSequence = 0);

    /// @brief Logs device changes and optional control events.
    void logInputEvents(const Input::System &input, const Input::Types::Frame &frame, bool includeTraceDetails, bool includeControlEvents);

    /// ------------------------------------------------------------
    /// Input cleanup
    /// ------------------------------------------------------------

    /// @brief Native source detach callback run before Input closes.
    using InputDetachCallback = IO::Types::Status (*)(void *context) noexcept;

    /// @brief Captures input health, optionally detaches native sources, and closes the input system.
    /// @param input Input session to close.
    /// @param detach Optional native source detach callback.
    /// @param context Context passed to detach.
    [[nodiscard]] bool finishInputSystem(Input::System &input, InputDetachCallback detach = nullptr, void *context = nullptr);
} // namespace GameWIP::Game::RuntimeDiagnostics
