/// @file win32_taskbar.cpp
/// @brief Implements Win32 taskbar integration.

#include "desktop/internal/results.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/shell.h"
#include "desktop/platform/win32/internal/win32_com.h"
#include "desktop/platform/win32/internal/win32_shell_helpers.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <commctrl.h>
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Taskbar helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;
        using Win32::convertTip;
        using Win32::createIcon;

        /// @brief Owns native taskbar resources associated with one portable TaskbarItem.
        struct TaskbarNative
        {
            // Bound Window and overlay icon
            HWND window = nullptr;
            HICON overlay = nullptr;

            // Thumbnail button icons and initialization state
            std::vector<HICON> buttonIcons;
            bool buttonsInitialized = false;
        };

        constexpr std::size_t kTaskbarButtonCapacity = 7;

        /// @brief Releases a set of taskbar-owned icon handles.
        void destroyIcons(std::span<HICON> icons) noexcept
        {
            for(HICON icon : icons)
            {
                if(icon != nullptr)
                {
                    DestroyIcon(icon);
                }
            }
        }

        /// @brief Applies taskbar progress state through the COM taskbar interface.
        [[nodiscard]] GameWIP::Status::Types::Status applyTaskbarProgress(TaskbarNative &native, const std::optional<Types::Shell::Progress> &progress) noexcept
        {
            ApartmentLease apartment;

            if(!apartment.status().ok())
            {
                return apartment.status();
            }

            ComPtr<ITaskbarList3> taskbar;
            HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            result = taskbar->HrInit();

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            TBPFLAG flag = TBPF_NOPROGRESS;

            if(progress.has_value())
            {
                switch(progress->state)
                {
                case Types::Shell::ProgressState::Normal:
                    flag = TBPF_NORMAL;
                    break;
                case Types::Shell::ProgressState::Indeterminate:
                    flag = TBPF_INDETERMINATE;
                    break;
                case Types::Shell::ProgressState::Paused:
                    flag = TBPF_PAUSED;
                    break;
                case Types::Shell::ProgressState::Error:
                    flag = TBPF_ERROR;
                    break;
                default:
                    return GameWIP::Desktop::Internal::statusFor(ErrorCode::InvalidArgument);
                }
            }
            result = taskbar->SetProgressState(native.window, flag);

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            if(progress.has_value() && progress->state != Types::Shell::ProgressState::Indeterminate)
            {
                const ULONGLONG value = static_cast<ULONGLONG>(std::llround(std::clamp(progress->fraction, 0.0, 1.0) * 1000.0));
                result = taskbar->SetProgressValue(native.window, value, 1000);

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Rebuilds taskbar thumbnail buttons while retaining icon cleanup on failure.
        [[nodiscard]] GameWIP::Status::Types::Status applyTaskbarButtons(
            TaskbarNative &native,
            std::span<const OwnedThumbnailButton> buttons,
            std::vector<HICON> &newIcons) noexcept
        {
            if(buttons.size() > kTaskbarButtonCapacity)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::SizeLimitExceeded);
            }

            const auto cleanupIcons = [&] noexcept
            {
                destroyIcons(newIcons);
                newIcons.clear();
            };
            try
            {
                ApartmentLease apartment;

                if(!apartment.status().ok())
                {
                    return apartment.status();
                }
                ComPtr<ITaskbarList3> taskbar;
                HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()));

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }

                result = taskbar->HrInit();

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }

                // Initialize native button slots and prepare their labels and icons.
                std::array<THUMBBUTTON, kTaskbarButtonCapacity> nativeButtons;
                newIcons.assign(kTaskbarButtonCapacity, nullptr);

                for(std::size_t index = 0; index < kTaskbarButtonCapacity; ++index)
                {
                    THUMBBUTTON &nativeButton = nativeButtons[index];
                    nativeButton.dwMask = THB_FLAGS | THB_TOOLTIP;
                    nativeButton.iId = 0x80000000U | static_cast<UINT>(index);
                    nativeButton.iBitmap = 0;
                    nativeButton.hIcon = nullptr;
                    nativeButton.szTip[0] = L'\0';
                    nativeButton.dwFlags = THBF_HIDDEN;

                    if(index >= buttons.size())
                    {
                        continue;
                    }
                    const OwnedThumbnailButton &button = buttons[index];
                    nativeButton.iId = button.id.value;
                    nativeButton.dwFlags = button.enabled ? THBF_ENABLED : THBF_DISABLED;
                    std::array<wchar_t, 260> tooltip{};
                    GameWIP::Status::Types::Status tipStatus = convertTip(button.label, tooltip);

                    if(!tipStatus.ok())
                    {
                        cleanupIcons();
                        return tipStatus;
                    }
                    std::copy(tooltip.begin(), tooltip.end(), nativeButton.szTip);

                    if(button.icon.has_value())
                    {
                        HICON icon = createIcon(*button.icon);

                        if(icon == nullptr)
                        {
                            cleanupIcons();
                            return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "create taskbar thumbnail icon");
                        }
                        newIcons[index] = icon;
                        nativeButton.dwMask |= THB_ICON;
                        nativeButton.hIcon = icon;
                    }
                }

                // Add the initial button set or update the existing set.
                if(!native.buttonsInitialized)
                {
                    result = taskbar->ThumbBarAddButtons(native.window, static_cast<UINT>(nativeButtons.size()), nativeButtons.data());
                }
                else
                {
                    result = taskbar->ThumbBarUpdateButtons(native.window, static_cast<UINT>(nativeButtons.size()), nativeButtons.data());
                }

                if(FAILED(result))
                {
                    cleanupIcons();
                    return statusFromHResult(result);
                }
                native.buttonsInitialized = true;
                return GameWIP::Status::Types::Status{};
            }
            catch(const std::bad_alloc &)
            {
                cleanupIcons();
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                cleanupIcons();
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Taskbar integration
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status openTaskbar(TaskbarItemState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeOpen))
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }

        if(state.window == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NotOpen);
        }
        const NativeHandleView nativeHandleView = nativeHandle(*state.window);

        if(nativeHandleView.window == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NotOpen);
        }

        try
        {
            auto *native = new TaskbarNative();
            native->window = static_cast<HWND>(nativeHandleView.window);
            state.platform = native;
            GameWIP::Status::Types::Status status = applyTaskbar(state);

            if(!status.ok())
            {
                closeTaskbarBestEffort(state);
            }

            return status;
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

    GameWIP::Status::Types::Status applyTaskbar(TaskbarItemState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeApply))
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }

        auto *native = static_cast<TaskbarNative *>(state.platform);

        if(native == nullptr || native->window == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NotOpen);
        }

        std::vector<HICON> newIcons;
        const auto cleanupIcons = [&] noexcept
        {
            destroyIcons(newIcons);
            newIcons.clear();
        };
        try
        {
            // Update buttons and progress before applying the overlay icon.
            GameWIP::Status::Types::Status status = applyTaskbarButtons(*native, state.thumbnailButtons, newIcons);

            if(!status.ok())
            {
                return status;
            }

            status = applyTaskbarProgress(*native, state.progress);

            if(!status.ok())
            {
                cleanupIcons();
                return status;
            }
            ApartmentLease apartment;

            if(!apartment.status().ok())
            {
                cleanupIcons();
                return apartment.status();
            }

            // Create the taskbar interface for the overlay update.
            ComPtr<ITaskbarList3> taskbar;
            HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()));

            if(FAILED(result))
            {
                cleanupIcons();
                return statusFromHResult(result);
            }

            result = taskbar->HrInit();

            if(FAILED(result))
            {
                cleanupIcons();
                return statusFromHResult(result);
            }
            HICON newOverlay = nullptr;

            if(state.overlayIcon.has_value())
            {
                newOverlay = createIcon(*state.overlayIcon);

                if(newOverlay == nullptr)
                {
                    cleanupIcons();
                    return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "create taskbar overlay icon");
                }
            }

            // Publish the new overlay before releasing retained icon resources.
            result = taskbar->SetOverlayIcon(native->window, newOverlay, L"");

            if(FAILED(result))
            {
                if(newOverlay != nullptr)
                {
                    DestroyIcon(newOverlay);
                }
                cleanupIcons();
                return statusFromHResult(result);
            }

            if(native->overlay != nullptr)
            {
                DestroyIcon(native->overlay);
            }
            native->overlay = newOverlay;

            // Replace button icon ownership after every native update succeeds.
            destroyIcons(native->buttonIcons);
            native->buttonIcons = std::move(newIcons);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            cleanupIcons();
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            cleanupIcons();
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Status closeTaskbar(TaskbarItemState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeClose))
        {
            return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }

        auto *native = static_cast<TaskbarNative *>(state.platform);

        if(native == nullptr)
        {
            state.open = false;
            return GameWIP::Status::Types::Status{};
        }

        // Clear the taskbar overlay before releasing its icon.
        if(native->overlay != nullptr)
        {
            ApartmentLease apartment;

            if(apartment.status().ok())
            {
                ComPtr<ITaskbarList3> taskbar;

                if(SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()))) &&
                   SUCCEEDED(taskbar->HrInit()))
                {
                    static_cast<void>(taskbar->SetOverlayIcon(native->window, nullptr, nullptr));
                }
            }
            DestroyIcon(native->overlay);
        }

        // Release thumbnail icons and detach the native state.
        destroyIcons(native->buttonIcons);
        delete native;
        state.platform = nullptr;
        state.open = false;
        return GameWIP::Status::Types::Status{};
    }

    void closeTaskbarBestEffort(TaskbarItemState &state) noexcept
    {
        static_cast<void>(closeTaskbar(state));
    }

    void notifyTaskbarWindowDestroyed(WindowState &window) noexcept
    {
        if(window.taskbarItem != nullptr)
        {
            TaskbarItemState *taskbar = window.taskbarItem;
            auto *native = static_cast<TaskbarNative *>(taskbar->platform);

            if(native != nullptr)
            {
                if(native->overlay != nullptr)
                {
                    DestroyIcon(native->overlay);
                }

                destroyIcons(native->buttonIcons);
                delete native;
            }
            window.taskbarItem = nullptr;
            taskbar->window = nullptr;
            taskbar->platform = nullptr;
            taskbar->open = false;
        }
    }

    void routeTaskbarCommand(WindowState &window, std::uint32_t commandId) noexcept
    {
        TaskbarItemState *taskbar = window.taskbarItem;

        if(taskbar == nullptr || taskbar->eventQueue == nullptr || !taskbar->open)
        {
            return;
        }

        for(const OwnedThumbnailButton &button : taskbar->thumbnailButtons)
        {
            if(button.id.value == commandId && button.enabled)
            {
                publishShellEvent(*taskbar->eventQueue, Types::Shell::Events::TaskbarThumbnailButtonInvoked{window.id, button.id});
                return;
            }
        }
    }
} // namespace GameWIP::Desktop::Platform
