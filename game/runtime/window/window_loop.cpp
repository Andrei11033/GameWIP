/// @file window_loop.cpp
/// @brief Pumps Window events, captures Input frames, and records loop telemetry.

#include "window_loop.h"

#include "runtime/diagnostics/runtime_diagnostics.h"
#include "runtime/logging.h"
#include "runtime/profiling.h"

#include "logger/logging.h"
#include "logger/message_format.h"
#include "desktop/events.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace
{
    namespace Color = GameWIP::Profiling::Color;
    namespace Logger = GameWIP::Logger;
    namespace Diagnostics = GameWIP::Game::RuntimeDiagnostics;
    using LogArea = GameWIP::Game::Logging::Area;
} // namespace

namespace GameWIP::Game::Runtime
{
    /// ------------------------------------------------------------
    /// Event-loop summary
    /// ------------------------------------------------------------

    void logWindowEventLoopSummary(const Desktop::Window &window, const EventLoopSummary &summary, double elapsedSeconds, std::string_view outcome)
    {
        if(!Logger::shouldLog(Logger::Types::Level::Debug, LogArea::Window))
        {
            return;
        }

        const auto queue = window.eventQueueInfo();
        Logger::Record record = Logger::record();
        record.field("outcome", outcome)
            .field("window_events", summary.eventsDrained)
            .field("input_events", summary.inputEvents)
            .field("device_changes", summary.deviceChanges)
            .field("window_events_dropped", queue.droppedEvents)
            .field("input_events_dropped", summary.inputEventsDropped);

        const bool logTrace = Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Window);
        if(logTrace)
        {
            record.field("event_pumps", summary.pumps)
                .field("events_dropped_by_pump", summary.eventsDropped)
                .field("input_captures", summary.inputCaptures)
                .field("input_capture_failures", summary.inputCaptureFailures)
                .field("skipped_ticks", summary.skippedTicks)
                .field("total_wait_ms", summary.totalWaitMilliseconds)
                .field("maximum_wait_ms", summary.maximumWaitMilliseconds);
            Logger::trace(LogArea::Window, record, "Runtime event loop summary after {:.1f}s", Logger::number(elapsedSeconds));
        }
        else
        {
            Logger::debug(LogArea::Window, record, "Runtime event summary after {:.1f}s", Logger::number(elapsedSeconds));
        }
    }

    /// ------------------------------------------------------------
    /// Window and Input pumping
    /// ------------------------------------------------------------

    bool pumpWindowEvents(
        Desktop::Window &window,
        Input::System &input,
        Input::Types::Frame &inputFrame,
        std::chrono::nanoseconds loopInterval,
        bool includeWindowEventDetails,
        bool includeInputTraceDetails,
        bool includeInputControlEvents,
        EventLoopSummary &summary)
    {
        using Clock = std::chrono::steady_clock;
        const Clock::duration cadence = std::chrono::duration_cast<Clock::duration>(loopInterval);
        std::uint64_t reportedDroppedEvents = 0;
        bool inputCaptureFailureReported = false;
        auto nextPumpAt = Clock::now() + cadence;

        while(true)
        {
            GAMEWIP_PROFILE_ZONE("Runtime event cycle", Color::kFrame);
            Desktop::Types::Events::PumpResult events;

            // Wait for the next combined Window and Input tick.
            if(!window.hasCloseRequest())
            {
                GAMEWIP_PROFILE_ZONE("Wait for runtime tick", Color::kWait);
                const auto waitStart = Clock::now();
                std::this_thread::sleep_until(nextPumpAt);
                const std::chrono::duration<double, std::milli> waited = Clock::now() - waitStart;
                GAMEWIP_PROFILE_PLOT("Runtime wait (ms)", waited.count());
                ++summary.pumps;
                summary.totalWaitMilliseconds += waited.count();
                summary.maximumWaitMilliseconds = std::max(summary.maximumWaitMilliseconds, waited.count());
                events = Desktop::Events::poll();
                summary.eventsDropped += events.eventsDropped;
            }

            // Drain Window events before capturing Input for this tick.
            std::size_t drained = 0;
            {
                GAMEWIP_PROFILE_ZONE("Drain Window Events", Color::kFrame);
                Desktop::Types::Event event;
                while(window.popEvent(event))
                {
                    Diagnostics::logWindowEvent(event, includeWindowEventDetails);
                    ++drained;
                }
            }
            summary.eventsDrained += drained;
            GAMEWIP_PROFILE_PLOT("Window events per pump", drained);

            // Report new queue drops once, with the current queue capacity.
            const auto queue = window.eventQueueInfo();
            if(queue.droppedEvents > reportedDroppedEvents)
            {
                Logger::warn(
                    LogArea::Window,
                    Logger::record()
                        .field("events_dropped_this_pump", events.eventsDropped)
                        .field("events_dropped_total", queue.droppedEvents)
                        .field("queue_capacity", queue.capacity),
                    "Window event queue dropped events");
                reportedDroppedEvents = queue.droppedEvents;
            }

            // Capture Input and report only the first failure until it resumes.
            Input::Types::CaptureResult capture;
            {
                GAMEWIP_PROFILE_ZONE("Capture Input Frame", Color::kInput);
                capture = input.capture(inputFrame);
            }

            if(!capture.status.ok())
            {
                ++summary.inputCaptureFailures;
                if(!inputCaptureFailureReported)
                {
                    Diagnostics::reportWarning(LogArea::Input, "Input capture failed", capture.status);
                    inputCaptureFailureReported = true;
                }
            }
            else
            {
                // Report recovery after a failed capture.
                if(inputCaptureFailureReported)
                {
                    Logger::info(LogArea::Input, "Input capture resumed");
                    inputCaptureFailureReported = false;
                }

                // Update capture totals and profiling data.
                ++summary.inputCaptures;
                summary.inputEvents += inputFrame.events().size();
                summary.inputEventsDropped += inputFrame.droppedEvents();
                GAMEWIP_PROFILE_PLOT("Input events per capture", inputFrame.events().size());
                GAMEWIP_PROFILE_PLOT("Input event drops per capture", inputFrame.droppedEvents());

                // Count device changes in this frame.
                for(const Input::Types::Event &inputEvent : inputFrame.events())
                {
                    if(inputEvent.kind == Input::Types::EventKind::DeviceConnected ||
                       inputEvent.kind == Input::Types::EventKind::DeviceDisconnected || inputEvent.kind == Input::Types::EventKind::DeviceChanged)
                    {
                        ++summary.deviceChanges;
                    }
                }

                // Report the captured input events.
                {
                    GAMEWIP_PROFILE_ZONE("Log Input Events", Color::kInput);
                    Diagnostics::logInputEvents(input, inputFrame, includeInputTraceDetails, includeInputControlEvents);
                }
            }

            // Stop on pump failure or a close request before scheduling another tick.
            if(!events.status.ok())
            {
                Diagnostics::reportFailure(LogArea::Window, "Event pump failed", events.status);
                return false;
            }

            if(window.hasCloseRequest())
            {
                break;
            }
            Profiling::frameMark();

            nextPumpAt += cadence;
            const auto cycleEnd = Clock::now();
            while(nextPumpAt <= cycleEnd)
            {
                ++summary.skippedTicks;
                nextPumpAt += cadence;
            }
        }

        return true;
    }
} // namespace GameWIP::Game::Runtime
