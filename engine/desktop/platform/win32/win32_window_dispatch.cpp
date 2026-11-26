/// @file win32_window_dispatch.cpp
/// @brief Win32 event pumping and wait dispatch.

#include "desktop/internal/results.h"
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
                dispatcher_.activePumpInfo = nullptr;
                dispatcher_.activePumpStatus = nullptr;
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
        Types::Events::PumpInfo info;
        GameWIP::Status::Types::Status status;

        // Reject reentrant pumps and injected failures before publishing active state.
        if(current.pumping)
        {
            return Types::Events::PumpResult::failure(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy));
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::EventPump))
        {
            return Types::Events::PumpResult::failure(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NativeFailure));
        }

        current.pumping = true;
        current.activePumpInfo = &info;
        current.activePumpStatus = &status;
        const ActivePumpScope activePump(current);

        // Surface deferred cleanup failures before considering a blocking wait.
        if(current.deferredPumpFailure)
        {
            status = std::move(*current.deferredPumpFailure);
            current.deferredPumpFailure.reset();
        }
        pruneAbandonedStates(current);
        // Callback/deferred cleanup failures are already the result of this pump; never
        // enter a blocking wait after an error is ready to return.
        if(!status.ok())
        {
            return GameWIP::Desktop::Internal::result(std::move(status), std::move(info));
        }

        // Avoid waiting when no native message source is active.
        if(current.windows.empty() && current.childSurfaces.empty() && (!current.progressDialogs || current.progressDialogs->empty()) &&
           !hasShellEventSources())
        {
            return GameWIP::Status::Types::Result<Types::Events::PumpInfo>::success(std::move(info));
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
                info.timedOut = true;
                return GameWIP::Status::Types::Result<Types::Events::PumpInfo>::success(std::move(info));
            }

            if(waitResult == WAIT_FAILED)
            {
                status = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "MsgWaitForMultipleObjectsEx");
                return GameWIP::Desktop::Internal::result(std::move(status), std::move(info));
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
                GameWIP::Status::Types::Status cursorStatus = applyCursorState(*state);

                if(!cursorStatus.ok() && status.ok())
                {
                    status = std::move(cursorStatus);
                }
            }
        }
        return GameWIP::Desktop::Internal::result(std::move(status), std::move(info));
    }
} // namespace GameWIP::Desktop::Platform
