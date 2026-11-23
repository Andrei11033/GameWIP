/// @file win32_window_dispatch.cpp
/// @brief Win32 event pumping and wait dispatch.

#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/internal/accessibility_state.h"
#include "desktop/platform/shell.h"
#include <algorithm>
#include <utility>

namespace GameWIP::Desktop::Platform
{
    namespace
    {
        /// @brief Clears the active pump marker on every return path.
        class ActivePumpScope final
        {
        public:
            explicit ActivePumpScope(Dispatcher &dispatcher) noexcept
                : dispatcher_(dispatcher)
            {
            }

            ~ActivePumpScope() noexcept
            {
                dispatcher_.activeResult = nullptr;
                dispatcher_.pumping = false;
            }

            ActivePumpScope(const ActivePumpScope &) = delete;
            ActivePumpScope &operator=(const ActivePumpScope &) = delete;

        private:
            Dispatcher &dispatcher_;
        };
    } // namespace

    /// @brief Pumps the owner-thread dispatcher and optionally waits for native input.
    Types::Events::PumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept
    {
        Dispatcher &current = dispatcher();
        Types::Events::PumpResult result;

        // Reject reentrant pumps and injected failures before publishing active state.
        if(current.pumping)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return result;
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::EventPump))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            return result;
        }

        current.pumping = true;
        current.activeResult = &result;
        const ActivePumpScope activePump(current);

        // Surface deferred cleanup failures before considering a blocking wait.
        if(current.deferredPumpFailure)
        {
            result.status = std::move(*current.deferredPumpFailure);
            current.deferredPumpFailure.reset();
        }
        pruneAbandonedStates(current);
        // Callback/deferred cleanup failures are already the result of this pump; never
        // enter a blocking wait after an error is ready to return.
        if(!result.status.ok())
        {
            return result;
        }

        // Avoid waiting when no native message source is active.
        if(current.windows.empty() && current.childSurfaces.empty() && (!current.progressDialogs || current.progressDialogs->empty()) &&
           !hasShellEventSources())
        {
            return result;
        }

        // Pending UI Automation actions must be drained without blocking.
        bool accessibilityPending = false;

        for(const WindowState *state : current.windows)
        {
            if(state && state->accessibility)
            {
                const auto runtime = state->accessibility->runtime.load();

                if(runtime && runtime->live.load())
                {
                    std::scoped_lock lock(runtime->actionMutex, runtime->notificationMutex);
                    accessibilityPending = accessibilityPending || runtime->actionCount != 0 || runtime->notificationCount != 0;
                }
            }
        }

        if(wait && !accessibilityPending)
        {
            DWORD milliseconds = INFINITE;

            if(timeout != Events::kWaitForever)
            {
                const auto maximum = static_cast<std::int64_t>(INFINITE - 1);
                milliseconds = static_cast<DWORD>(std::min<std::int64_t>(timeout.count(), maximum));
            }
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

            if(waitResult == WAIT_TIMEOUT)
            {
                result.timedOut = true;
                return result;
            }

            if(waitResult == WAIT_FAILED)
            {
                result.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MsgWaitForMultipleObjectsEx");
                return result;
            }
        }

        // Drain queued native messages and retain display-change state.
        bool receivedDisplayChange = false;
        MSG message{};
        const UINT wake = wakeMessage();

        while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if(wake != 0 && message.message == wake)
            {
                continue;
            }

            if(message.message == WM_QUIT)
            {
                for(WindowState *state : current.windows)
                {
                    if(state != nullptr)
                    {
                        static_cast<void>(Internal::requestClose(*state, Types::Events::CloseRequestSource::System));
                    }
                }
                continue;
            }

            if(message.message == WM_DISPLAYCHANGE)
            {
                receivedDisplayChange = true;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const bool displayColorChanged = consumeDisplayColorConfigurationChange();

        // Synthesize color-change events when Windows omitted WM_DISPLAYCHANGE.
        if(!receivedDisplayChange && displayColorChanged)
        {
            for(WindowState *state : current.windows)
            {
                if(state != nullptr)
                {
                    routeEvent(*state, Types::Events::DisplayConfigurationChanged{});
                }
            }
        }

        // Publish deferred accessibility state and refresh relative cursors.
        for(WindowState *state : current.windows)
        {
            if(state != nullptr)
            {
                publishAccessibilityGeometry(*state);
                drainAccessibilityNotifications(*state);
            }

            if(state != nullptr && state->platform && state->cursorMode == Types::CursorMode::Relative && state->focused)
            {
                IO::Types::Status cursorStatus = applyCursorState(*state);

                if(!cursorStatus.ok() && result.status.ok())
                {
                    result.status = std::move(cursorStatus);
                }
            }
        }
        return result;
    }
} // namespace GameWIP::Desktop::Platform
