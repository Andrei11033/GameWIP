/// @file win32_window_dispatch.cpp
/// @brief Win32 event pumping and wait dispatch.

#include "window/platform/win32/internal/win32_window_backend.h"
#include "window/platform/win32/internal/win32_compat.h"

#include "window/native/win32.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <new>
#include <utility>

namespace GameWIP::Window::Detail::Platform
{
    Types::Events::PumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept
    {
        Dispatcher &current = dispatcher();
        pruneAbandonedStates(current);
        Types::Events::PumpResult result;
        if (current.windows.empty() && current.childSurfaces.empty())
            return result;
        if (current.pumping)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return result;
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::EventPump))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            return result;
        }

        current.pumping = true;
        current.activeResult = &result;
        if (wait)
        {
            DWORD milliseconds = INFINITE;
            if (timeout != Events::kWaitForever)
            {
                const auto maximum = static_cast<std::int64_t>(INFINITE - 1);
                milliseconds = static_cast<DWORD>(std::min<std::int64_t>(timeout.count(), maximum));
            }
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (waitResult == WAIT_TIMEOUT)
            {
                result.timedOut = true;
                current.activeResult = nullptr;
                current.pumping = false;
                return result;
            }
            if (waitResult == WAIT_FAILED)
            {
                result.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MsgWaitForMultipleObjectsEx");
                current.activeResult = nullptr;
                current.pumping = false;
                return result;
            }
        }

        bool receivedDisplayChange = false;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if (message.message == wakeMessage())
                continue;
            if (message.message == WM_QUIT)
            {
                for (WindowState *state : current.windows)
                {
                    if (state != nullptr)
                        static_cast<void>(Detail::requestClose(*state, Types::Events::CloseRequestSource::System));
                }
                continue;
            }
            if (message.message == WM_DISPLAYCHANGE)
                receivedDisplayChange = true;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const bool displayColorChanged = consumeDisplayColorConfigurationChange();
        if (!receivedDisplayChange && displayColorChanged)
        {
            for (WindowState *state : current.windows)
            {
                if (state != nullptr)
                    routeEvent(*state, Types::Events::DisplayConfigurationChanged{});
            }
        }

        for (WindowState *state : current.windows)
        {
            if (state != nullptr && state->platform && state->cursorMode == Types::CursorMode::Relative && state->focused)
            {
                IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok() && result.status.ok())
                    result.status = std::move(cursorStatus);
            }
        }
        for (ChildSurfaceState *state : current.childSurfaces)
        {
            if (state != nullptr)
                refreshChildSurfaceScreenRect(*state);
        }
        current.activeResult = nullptr;
        current.pumping = false;
        return result;
    }
} // namespace GameWIP::Window::Detail::Platform
