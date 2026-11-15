/// @file runtime_diagnostics.h
/// @brief Internal structured diagnostic helpers used by the executable runtime.

#pragma once

#include "desktop/events.h"
#include "desktop/window.h"
#include "input/system.h"
#include "io/status.h"
#include "runtime/logging.h"

#if defined(_WIN32)
#include "input/win32.h"
#endif

#include <cstdint>
#include <string_view>

/// @brief Runtime-owned status and state reporting, separate from runtime sequencing.
namespace GameWIP::Game::RuntimeDiagnostics
{
    namespace Desktop = GameWIP::Desktop;
    namespace IO = GameWIP::IO;
    namespace Input = GameWIP::Input;

    // ------------------------------------------------------------
    // Operation status
    // ------------------------------------------------------------

    /// @brief Writes a final diagnostic without relying on Logger availability.
    void writeEmergencyMessage(std::string_view message) noexcept;

    /// @brief Writes a failed status with its portable and native codes to stderr.
    void writeEmergencyStatus(std::string_view action, const IO::Types::Status &status);

    /// @brief Synchronously reports a failed operation with its portable and native status.
    void reportFailure(Logging::Area area, std::string_view action, const IO::Types::Status &status);

    /// @brief Queues a recoverable operation issue with its portable and native status.
    void reportWarning(Logging::Area area, std::string_view action, const IO::Types::Status &status);

    // ------------------------------------------------------------
    // Window reports
    // ------------------------------------------------------------

    /// @brief Logs one queued Window event when event details are enabled.
    void logWindowEvent(const Desktop::Types::Event &event, bool includeDetails);

    /// @brief Logs Window startup and, at Debug, its cached state and drawable extents.
    void logWindowSnapshot(const Desktop::Window &window);

    // ------------------------------------------------------------
    // Input inventory and events
    // ------------------------------------------------------------

    /// @brief Logs concise device summaries and optional bounded descriptors.
    void logInputDevices(Input::System &input, bool includeTraceDetails);

    /// @brief Logs one bounded grouped device descriptor and optional device event at Trace.
    void logInputDeviceTrace(
        const Input::Types::DeviceInfo *device,
        Input::Types::DeviceId id,
        const Input::Types::Event *event = nullptr,
        std::uint64_t captureSequence = 0);

    /// @brief Logs device changes and optional control events.
    void logInputEvents(Input::System &input, const Input::Types::Frame &frame, bool includeTraceDetails, bool includeControlEvents);

    // ------------------------------------------------------------
    // Input cleanup
    // ------------------------------------------------------------

    /// @brief Captures input health and explicitly closes the input system.
    [[nodiscard]] bool finishInputSystem(Input::System &input);

#if defined(_WIN32)

    // ------------------------------------------------------------
    // Win32 backend reports and cleanup
    // ------------------------------------------------------------

    /// @brief Logs selected and available Win32 backend states at Trace.
    void logInputBackends(Input::Platform::Win32::Attachment &attachment);

    /// @brief Captures input health, detaches the Win32 source, and explicitly closes the input system.
    [[nodiscard]] bool finishInputSystem(Input::System &input, Input::Platform::Win32::Attachment &attachment);
#endif
} // namespace GameWIP::Game::RuntimeDiagnostics
