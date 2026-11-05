/// @file desktop_win32.h
/// @brief Optional Win32 integration between GameWIP Input and Desktop.

#pragma once

#if defined(_WIN32)

#include "desktop/native/win32.h"
#include "input/win32.h"

#include <cstdint>

namespace GameWIP::Input::Platform::Win32::AdapterDetail
{
    inline IO::Types::Status forwardMessage(void *context, HWND, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        auto *attachment = static_cast<Attachment *>(context);

        if (attachment == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }

        bool handled = false;

        return attachment
            ->handleMessage(static_cast<std::uint32_t>(message), static_cast<std::uint64_t>(wParam), static_cast<std::int64_t>(lParam), handled);
    }
} // namespace GameWIP::Input::Platform::Win32::AdapterDetail

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Attaches Win32 Input to an open Desktop Window.
    /// @details Registers native Input and Desktop message delivery as one operation.
    [[nodiscard]] inline IO::Types::Status attachToWindow(System &system, Attachment &attachment, GameWIP::Desktop::Window &window) noexcept
    {
        const auto nativeHandle = GameWIP::Desktop::Native::Win32::getHandle(window);

        if (!nativeHandle.ok() || nativeHandle->window == nullptr)
        {
            return nativeHandle.status;
        }

        const IO::Types::Status inputStatus = attachment.attach(system, static_cast<void *>(nativeHandle->window));

        if (!inputStatus.ok())
        {
            return inputStatus;
        }

        const IO::Types::Status sinkStatus = GameWIP::Desktop::Native::Win32::attachMessageSink(window, &AdapterDetail::forwardMessage, &attachment);

        if (!sinkStatus.ok())
        {
            static_cast<void>(attachment.detach());
            return sinkStatus;
        }

        return IO::successStatus();
    }
} // namespace GameWIP::Input::Platform::Win32

#endif
