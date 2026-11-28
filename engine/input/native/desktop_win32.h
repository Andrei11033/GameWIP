/// @file desktop_win32.h
/// @brief Optional Win32 integration between GameWIP Input and Desktop.

#pragma once

#if defined(_WIN32)

#include "desktop/native/win32.h"
#include "input/win32.h"

#include <cstdint>

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Attaches Win32 Input to an open Desktop Window.
    /// @details Registers native Input and Desktop message delivery as one operation.
    [[nodiscard]] inline GameWIP::Status::Types::Result<void> attachToWindow(
        System &system,
        Attachment &attachment,
        GameWIP::Desktop::Window &window) noexcept
    {
        const auto nativeHandle = GameWIP::Desktop::Native::Win32::getHandle(window);
        if(!nativeHandle.ok())
        {
            return GameWIP::Status::Types::Result<void>::failure(nativeHandle.status());
        }

        if(nativeHandle.value().window == nullptr)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(GameWIP::Status::Types::Code::NotOpen));
        }

        const GameWIP::Desktop::Native::Win32::MessageSink forwardMessage =
            [](void *context, HWND, UINT message, WPARAM wParam, LPARAM lParam) noexcept -> GameWIP::Status::Types::Result<void>
        {
            auto *messageAttachment = static_cast<Attachment *>(context);
            if(messageAttachment == nullptr)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(GameWIP::Status::Types::Code::InvalidArgument));
            }

            const auto result = messageAttachment->handleMessage(
                static_cast<std::uint32_t>(message),
                static_cast<std::uint64_t>(wParam),
                static_cast<std::int64_t>(lParam));
            return result.ok() ? GameWIP::Status::Types::Result<void>::success() : GameWIP::Status::Types::Result<void>::failure(result.status());
        };

        const auto inputStatus = attachment.attach(system, static_cast<void *>(nativeHandle.value().window));
        if(!inputStatus.ok())
        {
            return GameWIP::Status::Types::Result<void>::failure(inputStatus.status());
        }

        const auto sinkStatus = GameWIP::Desktop::Native::Win32::attachMessageSink(window, forwardMessage, &attachment);
        if(!sinkStatus.ok())
        {
            static_cast<void>(attachment.detach());
            return GameWIP::Status::Types::Result<void>::failure(sinkStatus.status());
        }

        return GameWIP::Status::Types::Result<void>::success();
    }
} // namespace GameWIP::Input::Platform::Win32

#endif
