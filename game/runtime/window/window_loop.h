/// @file window_loop.h
/// @brief Window and Input event pumping with aggregate runtime telemetry.

#pragma once

#include "desktop/window.h"
#include "input/frame.h"
#include "input/system.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

/// @brief Internal event-loop behavior shared by the runtime coordinator.
namespace GameWIP::Game::Runtime
{
    namespace Input = GameWIP::Input;

    /// @brief Aggregate timing, pump, and event counts for one window session.
    struct EventLoopSummary
    {
        // Work completed
        std::size_t pumps = 0;                ///< Number of successful or failed pump calls.
        std::size_t eventsDrained = 0;        ///< Queued Window events removed by the runtime.
        std::size_t inputCaptures = 0;        ///< Input frames captured during the session.
        std::size_t inputEvents = 0;          ///< Input events observed in captured frames.
        std::size_t deviceChanges = 0;        ///< Input device connect, disconnect, or descriptor events.
        std::size_t inputCaptureFailures = 0; ///< Input frame capture failures.
        std::size_t skippedTicks = 0;         ///< Cadence ticks skipped after a slow runtime cycle.

        // Work dropped
        std::uint64_t eventsDropped = 0;      ///< Events reported dropped across pump calls.
        std::uint64_t inputEventsDropped = 0; ///< Input events dropped before captured frames.

        // Wait timing
        double totalWaitMilliseconds = 0.0;   ///< Total elapsed time waiting for the next cadence tick.
        double maximumWaitMilliseconds = 0.0; ///< Longest wait for a cadence tick.
    };

    // ------------------------------------------------------------
    // Event-loop summary
    // ------------------------------------------------------------

    /// @brief Logs aggregate loop behavior for normal closure and pump failures.
    void logWindowEventLoopSummary(const Desktop::Window &window, const EventLoopSummary &summary, double elapsedSeconds, std::string_view outcome);

    // ------------------------------------------------------------
    // Window and Input pumping
    // ------------------------------------------------------------

    /// @brief Pumps Window events and captures Input until close or a Window pump failure.
    [[nodiscard]] bool pumpWindowEvents(
        Desktop::Window &window,
        Input::System &input,
        Input::Types::Frame &inputFrame,
        std::chrono::nanoseconds loopInterval,
        bool includeWindowEventDetails,
        bool includeInputTraceDetails,
        bool includeInputControlEvents,
        EventLoopSummary &summary);
} // namespace GameWIP::Game::Runtime
