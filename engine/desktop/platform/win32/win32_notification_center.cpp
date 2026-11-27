/// @file win32_notification_center.cpp
/// @brief Implements the Win32 notification-center bridge.

#include "desktop/internal/results.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/shell.h"
#include "desktop/platform/win32/internal/win32_shell_helpers.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <new>
#include <optional>
#include <string>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Notification helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;
        using Win32::kTrayCallbackMessage;

        /// @brief Owns the hidden message HWND used for one notification center.
        struct NotificationNative
        {
            // Portable callback owner
            NotificationCenterState *state = nullptr;

            // Hidden message window
            HWND window = nullptr;
        };

        inline constexpr wchar_t kNotificationWindowClassName[] = L"GameWIP.Shell.NotificationMessage";

        [[nodiscard]] GameWIP::Status::Types::Status ensureNotificationWindowClass() noexcept;

        /// @brief Rejects notification features unsupported by the Win32 balloon fallback.
        [[nodiscard]] GameWIP::Status::Types::Status validateBasicNotification(
            const OwnedNotificationDescription &description,
            std::wstring &title,
            std::wstring &body,
            DWORD &nativeCode)
        {
            if(!description.actions.empty() || !description.inputs.empty() || !description.media.empty() || description.schedule.has_value() ||
               !description.group.empty() || !description.badge.empty() || description.progress.has_value() ||
               description.urgency != Types::Notifications::Urgency::Normal)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unsupported);
            }

            if(!utf8ToUtf16(description.title, title, nativeCode) || !utf8ToUtf16(description.body, body, nativeCode))
            {
                return GameWIP::Desktop::Internal::statusFor(
                    ErrorCode::EncodingFailed,
                    nativeCode,
                    GameWIP::Desktop::Internal::NativeCodeDomain::Win32Error);
            }

            if(title.size() >= ARRAYSIZE(NOTIFYICONDATAW{}.szInfoTitle) || body.size() >= ARRAYSIZE(NOTIFYICONDATAW{}.szInfo))
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::SizeLimitExceeded);
            }

            return GameWIP::Status::Types::Status{};
        }

        LRESULT CALLBACK notificationWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *native = reinterpret_cast<NotificationNative *>(GetWindowLongPtrW(window, GWLP_USERDATA));

            if(message == WM_NCCREATE)
            {
                // Bind the message window to its portable notification state.
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
                native = static_cast<NotificationNative *>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(native));

                if(native != nullptr)
                {
                    native->window = window;
                    retainShellMessageWindow();
                }
            }

            if(native != nullptr && message == WM_NCDESTROY)
            {
                // Clear native ownership before forwarding final destruction.
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                native->window = nullptr;
                releaseShellMessageWindow();
                return DefWindowProcW(window, message, wParam, lParam);
            }

            if(native == nullptr || native->state == nullptr)
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }

            if(message == kTrayCallbackMessage)
            {
                // Translate shell balloon callbacks into queued notifications.
                const std::uint64_t id = static_cast<UINT>(wParam);

                if(native->state->eventQueue == nullptr)
                {
                    return 0;
                }

                if(lParam == NIN_BALLOONUSERCLICK)
                {
                    publishShellEvent(
                        *native->state->eventQueue,
                        Types::Shell::Events::NotificationActivated{Types::Shell::NotificationId{id}, std::nullopt, {}});
                }
                else if(lParam == NIN_BALLOONTIMEOUT)
                {
                    publishShellEvent(
                        *native->state->eventQueue,
                        Types::Shell::Events::NotificationDismissed{
                            Types::Shell::NotificationId{id},
                            Types::Shell::Events::NotificationDismissReason::Expired});
                }

                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        [[nodiscard]] GameWIP::Status::Types::Status ensureNotificationWindowClass() noexcept
        {
            WNDCLASSEXW existing{};
            existing.cbSize = sizeof(existing);
            HINSTANCE instance = GetModuleHandleW(nullptr);

            if(GetClassInfoExW(instance, kNotificationWindowClassName, &existing) != FALSE)
            {
                return GameWIP::Status::Types::Status{};
            }

            WNDCLASSEXW classInfo{};
            classInfo.cbSize = sizeof(classInfo);
            classInfo.lpfnWndProc = notificationWindowProc;
            classInfo.hInstance = instance;
            classInfo.lpszClassName = kNotificationWindowClassName;

            if(RegisterClassExW(&classInfo) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "RegisterClassExW notification window");
            }

            return GameWIP::Status::Types::Status{};
        }

        [[nodiscard]] GameWIP::Status::Types::Status showBasicNotification(
            NotificationNative &native,
            std::uint64_t id,
            const OwnedNotificationDescription &description,
            bool add)
        {
            std::wstring title;
            std::wstring body;
            DWORD nativeCode = ERROR_SUCCESS;
            GameWIP::Status::Types::Status status = validateBasicNotification(description, title, body, nativeCode);

            if(!status.ok())
            {
                return status;
            }

            // Prepare the notification and its callback identity.
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = native.window;
            data.uID = static_cast<UINT>(id);
            data.uFlags = add ? NIF_MESSAGE | NIF_ICON | NIF_INFO : NIF_INFO;
            data.uCallbackMessage = kTrayCallbackMessage;
            data.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            std::copy(title.begin(), title.end(), data.szInfoTitle);
            std::copy(body.begin(), body.end(), data.szInfo);
            data.dwInfoFlags = description.sound == Types::Notifications::Sound::Silent ? NIIF_NOSOUND : NIIF_INFO;
            data.uTimeout = 10000;

            if(add)
            {
                data.dwState = NIS_HIDDEN;
                data.dwStateMask = NIS_HIDDEN;
            }

            // Publish the balloon through the notification icon API.
            if(Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data) == FALSE)
            {
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "Shell_NotifyIconW notification");
            }

            return GameWIP::Status::Types::Status{};
        }

        [[nodiscard]] GameWIP::Status::Types::Status applyNotification(
            NotificationCenterState &state,
            std::uint64_t id,
            const OwnedNotificationDescription &description,
            bool add) noexcept
        {
            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeApply))
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
            }

            auto *native = static_cast<NotificationNative *>(state.platform);

            if(native == nullptr || native->window == nullptr)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::NotOpen);
            }

            try
            {
                return showBasicNotification(*native, id, description, add);
            }
            catch(const std::bad_alloc &)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Notification-center integration
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status openNotificationCenter(NotificationCenterState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeOpen))
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }

        GameWIP::Status::Types::Status status = ensureNotificationWindowClass();

        if(!status.ok())
        {
            return status;
        }

        try
        {
            // Create the hidden message window used to receive shell callbacks.
            auto *native = new NotificationNative();
            native->state = &state;
            state.platform = native;
            native->window = CreateWindowExW(
                0,
                kNotificationWindowClassName,
                L"GameWIP notifications",
                0,
                0,
                0,
                0,
                0,
                HWND_MESSAGE,
                nullptr,
                GetModuleHandleW(nullptr),
                native);

            if(native->window == nullptr)
            {
                state.platform = nullptr;
                delete native;
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "CreateWindowExW notification message window");
            }
            state.open = true;
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Status publishNotification(
        NotificationCenterState &state,
        std::uint64_t id,
        const OwnedNotificationDescription &description) noexcept
    {
        return applyNotification(state, id, description, true);
    }

    GameWIP::Status::Types::Status updateNotification(
        NotificationCenterState &state,
        std::uint64_t id,
        const OwnedNotificationDescription &description) noexcept
    {
        return applyNotification(state, id, description, false);
    }

    GameWIP::Status::Types::Status dismissNotification(NotificationCenterState &state, std::uint64_t id) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeApply))
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }

        auto *native = static_cast<NotificationNative *>(state.platform);

        if(native == nullptr || native->window == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NotOpen);
        }

        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = native->window;
        data.uID = static_cast<UINT>(id);

        if(Shell_NotifyIconW(NIM_DELETE, &data) == FALSE)
        {
            const DWORD nativeCode = GetLastError();

            if(nativeCode != ERROR_FILE_NOT_FOUND)
            {
                return statusFromWin32(ErrorCode::NativeFailure, nativeCode, "Shell_NotifyIconW dismiss notification");
            }
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status closeNotificationCenter(NotificationCenterState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeClose))
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }

        auto *native = static_cast<NotificationNative *>(state.platform);

        if(native == nullptr)
        {
            state.open = false;
            return GameWIP::Status::Types::Status{};
        }

        // Remove every balloon before destroying its callback window.
        for(const auto &entry : state.notifications)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = native->window;
            data.uID = static_cast<UINT>(entry.first);
            static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        }

        // Release the native window and clear the portable binding.
        if(native->window != nullptr)
        {
            DestroyWindow(native->window);
        }
        delete native;
        state.platform = nullptr;
        state.open = false;
        return GameWIP::Status::Types::Status{};
    }

    void closeNotificationCenterBestEffort(NotificationCenterState &state) noexcept
    {
        static_cast<void>(closeNotificationCenter(state));
    }
} // namespace GameWIP::Desktop::Platform
