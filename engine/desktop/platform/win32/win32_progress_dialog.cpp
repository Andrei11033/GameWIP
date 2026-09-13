/// @file win32_progress_dialog.cpp
/// @brief Modeless Win32 implementation of ProgressDialog.

#include "desktop/internal/dialogs_platform.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>

namespace GameWIP::Desktop::Detail::Platform
{
    struct ProgressDialogData;

    namespace
    {
        constexpr wchar_t kProgressClassName[] = L"GameWIP.Desktop.ProgressDialog";
        constexpr int kProgressRange = 10000;
        constexpr int kCancelControlId = 1;

        std::mutex progressClassMutex;
        std::size_t progressClassUsers = 0;
        HINSTANCE progressClassInstance = nullptr;
        bool progressClassOwned = false;

        [[nodiscard]] IO::Types::Status acquireProgressClass(HINSTANCE instance) noexcept;
        [[nodiscard]] IO::Types::Status releaseProgressClass() noexcept;
        [[nodiscard]] IO::Types::Status layoutProgressWindow(ProgressDialogData &data, UINT dpi) noexcept;
        [[nodiscard]] IO::Types::Status restoreOwnerAfterRemoval(const ProgressDialogState &state) noexcept;
        void queueProgressOwnerRestore(ProgressDialogState &state) noexcept;
        void cancelProgressOwnerRestore(ProgressDialogState &state) noexcept;
        [[nodiscard]] ProgressCloseResult finalizeNative(ProgressDialogState &state, bool restoreOwner) noexcept;
        void abandonNativeForThreadExit(ProgressDialogState &state) noexcept;
        void rollbackProgressOpenBestEffort(ProgressDialogState &state) noexcept;
    } // namespace

    struct ProgressDialogData
    {
        ProgressDialogState *owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND handle = nullptr;
        HWND heading = nullptr;
        HWND message = nullptr;
        HWND progress = nullptr;
        HWND cancel = nullptr;
        HWND nativeOwner = nullptr;
        DWORD ownerThreadId = 0;
        bool classReferenceHeld = false;
        bool registered = false;
        bool blockingOwner = false;
        bool destroying = false;
    };

    namespace
    {
        [[nodiscard]] LONG scaled(int value, UINT dpi) noexcept
        {
            return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kBaselineDpi));
        }

        IO::Types::Status layoutProgressWindow(ProgressDialogData &data, UINT dpi) noexcept
        {
            if (data.handle == nullptr)
            {
                return IO::successStatus();
            }
            RECT client{};
            if (GetClientRect(data.handle, &client) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "GetClientRect ProgressDialog");
            }
            const LONG margin = scaled(18, dpi);
            const LONG gap = scaled(10, dpi);
            const LONG headingHeight = scaled(26, dpi);
            const LONG messageHeight = scaled(40, dpi);
            const LONG progressHeight = scaled(20, dpi);
            const LONG buttonWidth = scaled(90, dpi);
            const LONG buttonHeight = scaled(28, dpi);
            const LONG width = std::max<LONG>(0, client.right - client.left - margin * 2);
            LONG top = margin;

            if (MoveWindow(data.heading, margin, top, width, headingHeight, TRUE) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MoveWindow ProgressDialog heading");
            }
            top += headingHeight + gap;
            if (MoveWindow(data.message, margin, top, width, messageHeight, TRUE) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MoveWindow ProgressDialog message");
            }
            top += messageHeight + gap;
            if (MoveWindow(data.progress, margin, top, width, progressHeight, TRUE) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MoveWindow ProgressDialog progress");
            }
            if (data.cancel != nullptr)
            {
                if (MoveWindow(
                        data.cancel,
                        client.right - margin - buttonWidth,
                        client.bottom - margin - buttonHeight,
                        buttonWidth,
                        buttonHeight,
                        TRUE) == FALSE)
                {
                    return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MoveWindow ProgressDialog cancel");
                }
            }
            return IO::successStatus();
        }

        [[nodiscard]] LRESULT CALLBACK progressWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *data = reinterpret_cast<ProgressDialogData *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
                data = static_cast<ProgressDialogData *>(create->lpCreateParams);
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
                const DWORD error = GetLastError();
                if (previous == 0 && error != ERROR_SUCCESS)
                {
                    return FALSE;
                }
                data->handle = window;
            }
            if (data == nullptr)
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }

            switch (message)
            {
            case WM_COMMAND:
                if (LOWORD(wParam) == kCancelControlId && data->owner != nullptr && data->owner->cancelable)
                {
                    data->owner->cancelRequested = true;
                    return 0;
                }
                break;
            case WM_CLOSE:
                if (data->owner != nullptr && data->owner->cancelable)
                {
                    data->owner->cancelRequested = true;
                }
                return 0;
            case WM_DPICHANGED:
            {
                const auto *suggested = reinterpret_cast<const RECT *>(lParam);
                if (SetWindowPos(
                        window,
                        nullptr,
                        suggested->left,
                        suggested->top,
                        suggested->right - suggested->left,
                        suggested->bottom - suggested->top,
                        SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
                {
                    recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowPos ProgressDialog DPI"));
                }
                const IO::Types::Status layout = layoutProgressWindow(*data, HIWORD(wParam));
                if (!layout.ok())
                {
                    recordPumpFailure(layout);
                }
                return 0;
            }
            case WM_NCDESTROY:
            {
                const bool unexpected = !data->destroying;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                data->handle = nullptr;
                data->heading = nullptr;
                data->message = nullptr;
                data->progress = nullptr;
                data->cancel = nullptr;
                if (unexpected && data->owner != nullptr)
                {
                    ProgressDialogState &state = *data->owner;
                    const bool blocked = data->blockingOwner;
                    data->blockingOwner = false;
                    if (data->registered)
                    {
                        unregisterOpenProgressDialog(state);
                        data->registered = false;
                    }
                    if (blocked && data->nativeOwner != nullptr)
                    {
                        const std::uint64_t ownerId = state.ownerId.value;
                        queueProgressOwnerRestore(state);
                        // This is only a wake: the intrusive dispatcher chain already owns the
                        // restoration obligation, so a failed post cannot strand the owner.
                        bool postFailed = false;
                        DWORD postError = ERROR_SUCCESS;
#if DESKTOP_INTERNAL_TEST_HOOKS
                        postFailed = Detail::consumeFailure(TestHooks::FailurePoint::ProgressOwnerRestoreWake);
                        if (postFailed)
                        {
                            postError = ERROR_NOT_ENOUGH_MEMORY;
                        }
#endif
                        if (!postFailed)
                        {
                            const UINT restoreMessage = ensureProgressOwnerRestoreMessage();
                            if (restoreMessage == 0)
                            {
                                postFailed = true;
                                postError = progressOwnerRestoreMessageError();
                            }
                            else
                            {
                                SetLastError(ERROR_SUCCESS);
                                if (PostMessageW(
                                        data->nativeOwner,
                                        restoreMessage,
                                        static_cast<WPARAM>(static_cast<std::uint32_t>(ownerId)),
                                        static_cast<LPARAM>(static_cast<std::uint32_t>(ownerId >> 32U))) == FALSE)
                                {
                                    postFailed = true;
                                    postError = GetLastError();
                                }
                            }
                        }
                        if (postFailed)
                        {
                            recordPumpFailure(statusFromWin32(
                                IO::Types::ErrorCode::Interrupted,
                                postError == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : postError,
                                "PostMessageW ProgressDialog owner restore"));
                        }
                    }
                    state.cancelRequested = false;
                    state.nativeDestroyedPendingFinalize = true;
                }
                break;
            }
            default:
                break;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        [[nodiscard]] IO::Types::Status acquireProgressClass(HINSTANCE instance) noexcept
        {
            std::scoped_lock lock(progressClassMutex);
            if (progressClassUsers != 0)
            {
                ++progressClassUsers;
                return IO::successStatus();
            }

            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = progressWindowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            windowClass.lpszClassName = kProgressClassName;
#if DESKTOP_INTERNAL_TEST_HOOKS
            if (Detail::consumeFailure(TestHooks::FailurePoint::ProgressClassRegistration))
            {
                return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);
            }
#endif
            if (RegisterClassExW(&windowClass) == 0)
            {
                const DWORD nativeCode = GetLastError();
                if (nativeCode != ERROR_CLASS_ALREADY_EXISTS)
                {
                    return statusFromWin32(IO::Types::ErrorCode::OpenFailed, nativeCode, "RegisterClassExW ProgressDialog");
                }
                progressClassOwned = false;
            }
            else
            {
                progressClassOwned = true;
            }
            progressClassInstance = instance;
            progressClassUsers = 1;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status releaseProgressClass() noexcept
        {
            std::scoped_lock lock(progressClassMutex);
            if (progressClassUsers == 0)
            {
                return IO::successStatus();
            }
            --progressClassUsers;
            if (progressClassUsers != 0 || !progressClassOwned)
            {
                return IO::successStatus();
            }
#if DESKTOP_INTERNAL_TEST_HOOKS
            if (Detail::consumeFailure(TestHooks::FailurePoint::ProgressClassRelease))
            {
                ++progressClassUsers;
                return IO::makeStatus(IO::Types::ErrorCode::CloseFailed);
            }
#endif
            if (UnregisterClassW(kProgressClassName, progressClassInstance) == FALSE)
            {
                ++progressClassUsers;
                return statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "UnregisterClassW ProgressDialog");
            }
            progressClassOwned = false;
            progressClassInstance = nullptr;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status convertProgressText(std::string_view text, std::wstring &wide) noexcept
        {
            try
            {
                DWORD nativeCode = ERROR_SUCCESS;
                if (!utf8ToUtf16(text, wide, nativeCode))
                {
                    return IO::makeStatus(unicodeConversionError(nativeCode, IO::Types::ErrorCode::EncodingFailed), nativeCode);
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(IO::Types::ErrorCode::Unknown);
            }
        }

        [[nodiscard]] IO::Types::Status setNativeText(HWND control, std::string_view text) noexcept
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            if (Detail::consumeFailure(TestHooks::FailurePoint::ProgressMutation))
            {
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            }
#endif
            std::wstring wide;
            IO::Types::Status status = convertProgressText(text, wide);
            if (!status.ok())
            {
                return status;
            }
            SetLastError(ERROR_SUCCESS);
            if (SetWindowTextW(control, wide.c_str()) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowTextW ProgressDialog");
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status applyProgressMode(ProgressDialogState &state, Types::Dialogs::Progress::Mode mode) noexcept
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            if (Detail::consumeFailure(TestHooks::FailurePoint::ProgressMutation))
            {
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            }
#endif
            ProgressDialogData &data = *state.platform;
            LONG_PTR style = 0;
            const IO::Types::Status queryStatus = queryWindowLong(data.progress, GWL_STYLE, style, "GetWindowLongPtrW ProgressDialog mode");
            if (!queryStatus.ok())
            {
                return queryStatus;
            }
            const LONG_PTR requestedStyle =
                mode == Types::Dialogs::Progress::Mode::Indeterminate ? style | PBS_MARQUEE : style & ~static_cast<LONG_PTR>(PBS_MARQUEE);
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(data.progress, GWL_STYLE, requestedStyle);
            const DWORD error = GetLastError();
            if (previous == 0 && error != ERROR_SUCCESS)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, error, "SetWindowLongPtrW ProgressDialog mode");
            }
            if (mode == Types::Dialogs::Progress::Mode::Indeterminate)
            {
                SendMessageW(data.progress, PBM_SETMARQUEE, TRUE, 30);
            }
            else
            {
                SendMessageW(data.progress, PBM_SETMARQUEE, FALSE, 0);
                const int position = static_cast<int>(std::lround(state.progress * kProgressRange));
                SendMessageW(data.progress, PBM_SETPOS, static_cast<WPARAM>(position), 0);
            }
            if (SetWindowPos(data.progress, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowPos ProgressDialog mode");
            }
            return IO::successStatus();
        }

        IO::Types::Status restoreOwnerAfterRemoval(const ProgressDialogState &state) noexcept
        {
            WindowState *owner = resolveWindowId(state.ownerId);
            if (owner != nullptr && state.platform && owner->platform && owner->platform->handle == state.platform->nativeOwner &&
                !owner->platform->destroying && IsWindow(owner->platform->handle) != FALSE && !windowHasBlockingProgressDialog(*owner))
            {
                EnableWindow(owner->platform->handle, owner->interactionEnabled ? TRUE : FALSE);
                if (IsWindowEnabled(owner->platform->handle) != (owner->interactionEnabled ? TRUE : FALSE))
                {
                    return statusFromWin32(IO::Types::ErrorCode::NativeFailure, ERROR_FUNCTION_FAILED, "EnableWindow ProgressDialog owner restore");
                }
            }
            return IO::successStatus();
        }

        void queueProgressOwnerRestore(ProgressDialogState &state) noexcept
        {
            if (state.ownerRestorePending)
            {
                return;
            }
            Dispatcher &current = dispatcher();
            state.pendingOwnerRestoreNext = current.pendingProgressOwnerRestoreHead;
            current.pendingProgressOwnerRestoreHead = &state;
            state.ownerRestorePending = true;
        }

        void cancelProgressOwnerRestore(ProgressDialogState &state) noexcept
        {
            if (!state.ownerRestorePending)
            {
                return;
            }
            ProgressDialogState **link = &dispatcher().pendingProgressOwnerRestoreHead;
            while (*link != nullptr && *link != &state)
            {
                link = &(*link)->pendingOwnerRestoreNext;
            }
            if (*link == &state)
            {
                *link = state.pendingOwnerRestoreNext;
            }
            state.pendingOwnerRestoreNext = nullptr;
            state.ownerRestorePending = false;
        }

        [[nodiscard]] ProgressCloseResult finalizeNative(ProgressDialogState &state, bool restoreOwner) noexcept
        {
            if (state.ownerRestorePending && restoreOwner)
            {
                IO::Types::Status restore = restoreOwnerAfterRemoval(state);
                if (!restore.ok())
                {
                    return {std::move(restore), false};
                }
            }
            cancelProgressOwnerRestore(state);
            if (!state.platform)
            {
                state.nativeDestroyedPendingFinalize = true;
                state.cancelRequested = false;
                return {IO::successStatus(), true};
            }
            ProgressDialogData &data = *state.platform;
            if (data.handle != nullptr)
            {
#if DESKTOP_INTERNAL_TEST_HOOKS
                if (Detail::consumeFailure(TestHooks::FailurePoint::Close))
                {
                    return {IO::makeStatus(IO::Types::ErrorCode::CloseFailed), false};
                }
#endif
                data.destroying = true;
                if (DestroyWindow(data.handle) == FALSE)
                {
                    data.destroying = false;
                    return {statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "DestroyWindow ProgressDialog"), false};
                }
            }

            if (data.registered)
            {
                unregisterOpenProgressDialog(state);
                data.registered = false;
            }
            const bool blocked = data.blockingOwner;
            data.blockingOwner = false;
            if (blocked && restoreOwner)
            {
                IO::Types::Status restore = restoreOwnerAfterRemoval(state);
                if (!restore.ok())
                {
                    queueProgressOwnerRestore(state);
                    return {std::move(restore), false};
                }
            }
            state.cancelRequested = false;
            state.nativeDestroyedPendingFinalize = true;
            if (data.classReferenceHeld)
            {
                IO::Types::Status status = releaseProgressClass();
                if (!status.ok())
                {
                    return {std::move(status), false};
                }
                data.classReferenceHeld = false;
            }
            state.platform.reset();
            return {IO::successStatus(), true};
        }

        void abandonNativeForThreadExit(ProgressDialogState &state) noexcept
        {
            if (!state.platform)
            {
                state.cancelRequested = false;
                state.nativeDestroyedPendingFinalize = true;
                return;
            }

            ProgressDialogData &data = *state.platform;
            if (data.registered)
            {
                unregisterOpenProgressDialog(state);
                data.registered = false;
            }
            data.blockingOwner = false;
            if (data.handle != nullptr)
            {
                data.owner = nullptr;
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR previous = SetWindowLongPtrW(data.handle, GWLP_USERDATA, 0);
                const DWORD error = GetLastError();
                if (previous == 0 && error != ERROR_SUCCESS)
                {
                    // This exceptional terminal path deliberately retains the allocation: a
                    // live HWND could otherwise dereference freed GWLP_USERDATA. Ordinary
                    // checked and deferred cleanup prevents this fail-safe leak. Thread exit
                    // destroys its HWNDs, while registered-class bookkeeping remains owned by
                    // process/module teardown rather than by the thread.
                    [[maybe_unused]] ProgressDialogData *const leakedData = state.platform.release();
                    state.cancelRequested = false;
                    state.nativeDestroyedPendingFinalize = true;
                    return;
                }
                data.handle = nullptr;
            }

            // A failed terminal close cannot safely unregister a class that may
            // still own an HWND; process/module teardown retains that class bookkeeping.
            data.classReferenceHeld = false;
            state.cancelRequested = false;
            state.nativeDestroyedPendingFinalize = true;
            state.platform.reset();
        }

        void rollbackProgressOpenBestEffort(ProgressDialogState &state) noexcept
        {
            // The original open failure remains authoritative; unresolved native state stays
            // owned by retained/deferred cleanup when best-effort finalization cannot finish.
            static_cast<void>(finalizeNative(state, true));
        }
    } // namespace

    void ProgressDialogDataDeleter::operator()(ProgressDialogData *data) const noexcept
    {
        if (data == nullptr)
        {
            return;
        }
        ProgressDialogState *portableOwner = data->owner;
        data->owner = nullptr;
        const bool onOwnerThread = data->ownerThreadId == GetCurrentThreadId();
        const auto finalizePortableFallback = [&]() noexcept
        {
            if (!onOwnerThread || portableOwner == nullptr)
            {
                return;
            }
            if (data->registered)
            {
                unregisterOpenProgressDialog(*portableOwner);
                data->registered = false;
            }
            const bool blocked = data->blockingOwner;
            data->blockingOwner = false;
            if (blocked)
            {
                static_cast<void>(restoreOwnerAfterRemoval(*portableOwner));
            }
        };
        if (data->handle != nullptr)
        {
            if (!onOwnerThread)
            {
                // This exceptional terminal fallback deliberately leaks storage because a live
                // HWND can still dereference GWLP_USERDATA. Ordinary dispatcher transfer owns
                // and finalizes this cleanup before deletion is considered.
                return;
            }
            data->destroying = true;
            if (DestroyWindow(data->handle) == FALSE)
            {
                data->destroying = false;
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR previous = SetWindowLongPtrW(data->handle, GWLP_USERDATA, 0);
                const DWORD error = GetLastError();
                if (previous == 0 && error != ERROR_SUCCESS)
                {
                    // Keep the allocation for the same live-HWND safety invariant above.
                    finalizePortableFallback();
                    return;
                }
                data->handle = nullptr;
                // The detached live HWND keeps the process class reference.
                data->classReferenceHeld = false;
            }
        }
        finalizePortableFallback();
        if (data->classReferenceHeld)
        {
            if (!releaseProgressClass().ok())
            {
                // Terminal fail-safe: retain the allocation rather than deleting state while the class reference could not be released safely.
                return;
            }
            data->classReferenceHeld = false;
        }
        delete data;
    }

    void restorePendingProgressOwners(Dispatcher &current) noexcept
    {
        ProgressDialogState **link = &current.pendingProgressOwnerRestoreHead;
        while (*link != nullptr)
        {
            ProgressDialogState &state = **link;
            if (!state.ownerRestorePending || !state.platform)
            {
                *link = state.pendingOwnerRestoreNext;
                state.pendingOwnerRestoreNext = nullptr;
                state.ownerRestorePending = false;
                continue;
            }

            WindowState *owner = resolveWindowId(state.ownerId);
            const HWND expectedOwner = state.platform->nativeOwner;
            if (owner == nullptr || !owner->platform || owner->platform->handle != expectedOwner || owner->platform->destroying ||
                IsWindow(expectedOwner) == FALSE)
            {
                *link = state.pendingOwnerRestoreNext;
                state.pendingOwnerRestoreNext = nullptr;
                state.ownerRestorePending = false;
                continue;
            }
            if (windowHasBlockingProgressDialog(*owner))
            {
                link = &state.pendingOwnerRestoreNext;
                continue;
            }

            const IO::Types::Status status = restoreOwnerAfterRemoval(state);
            if (!status.ok())
            {
                recordPumpFailure(status);
                link = &state.pendingOwnerRestoreNext;
                continue;
            }
            *link = state.pendingOwnerRestoreNext;
            state.pendingOwnerRestoreNext = nullptr;
            state.ownerRestorePending = false;
        }
    }

    DWORD progressOwnerNativeThreadId(const ProgressDialogState &state) noexcept
    {
        return state.platform ? state.platform->ownerThreadId : 0;
    }

    IO::Types::Status openProgress(ProgressDialogState &state, const Types::Dialogs::Progress::Description &description) noexcept
    {
        try
        {
            std::wstring title;
            std::wstring heading;
            std::wstring message;
            IO::Types::Status status = convertProgressText(description.title, title);
            if (!status.ok())
            {
                return status;
            }
            status = convertProgressText(description.heading, heading);
            if (!status.ok())
            {
                return status;
            }
            status = convertProgressText(description.message, message);
            if (!status.ok())
            {
                return status;
            }

            auto data = std::unique_ptr<ProgressDialogData, ProgressDialogDataDeleter>(new ProgressDialogData);
            data->owner = &state;
            data->ownerThreadId = GetCurrentThreadId();
            data->instance = GetModuleHandleW(nullptr);
            if (state.ownerId.isValid())
            {
                WindowState *owner = resolveWindowId(state.ownerId);
                if (owner == nullptr || !owner->platform || owner->platform->handle == nullptr)
                {
                    return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
                }
                data->nativeOwner = owner->platform->handle;
            }

            status = acquireProgressClass(data->instance);
            if (!status.ok())
            {
                return status;
            }
            data->classReferenceHeld = true;
            state.platform = std::move(data);
            ProgressDialogData &native = *state.platform;

            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
            if (InitCommonControlsEx(&controls) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "InitCommonControlsEx");
            }

            const UINT dpi = native.nativeOwner != nullptr ? dpiForWindow(native.nativeOwner) : dpiForWindow(nullptr);
            RECT bounds{0, 0, scaled(460, dpi), scaled(description.cancelable ? 190 : 160, dpi)};
            if (AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "AdjustWindowRectExForDpi ProgressDialog");
            }
#if DESKTOP_INTERNAL_TEST_HOOKS
            const bool failNativeCreation = Detail::consumeFailure(TestHooks::FailurePoint::NativeCreation);
#else
            constexpr bool failNativeCreation = false;
#endif
            native.handle = failNativeCreation ? nullptr
                                               : CreateWindowExW(
                                                     0,
                                                     kProgressClassName,
                                                     title.c_str(),
                                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                                     CW_USEDEFAULT,
                                                     CW_USEDEFAULT,
                                                     bounds.right - bounds.left,
                                                     bounds.bottom - bounds.top,
                                                     native.nativeOwner,
                                                     nullptr,
                                                     native.instance,
                                                     &native);
            if (failNativeCreation)
            {
                SetLastError(ERROR_GEN_FAILURE);
            }
            if (native.handle == nullptr)
            {
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "CreateWindowExW ProgressDialog");
            }
            registerOpenProgressDialog(state);
            native.registered = true;

            DWORD controlCreationError = ERROR_SUCCESS;
            native.heading = CreateWindowExW(
                0,
                L"STATIC",
                heading.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0,
                0,
                0,
                0,
                native.handle,
                nullptr,
                native.instance,
                nullptr);
            if (native.heading == nullptr && controlCreationError == ERROR_SUCCESS)
            {
                controlCreationError = GetLastError();
                if (controlCreationError == ERROR_SUCCESS)
                {
                    controlCreationError = ERROR_FUNCTION_FAILED;
                }
            }
            native.message = CreateWindowExW(
                0,
                L"STATIC",
                message.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0,
                0,
                0,
                0,
                native.handle,
                nullptr,
                native.instance,
                nullptr);
            if (native.message == nullptr && controlCreationError == ERROR_SUCCESS)
            {
                controlCreationError = GetLastError();
                if (controlCreationError == ERROR_SUCCESS)
                {
                    controlCreationError = ERROR_FUNCTION_FAILED;
                }
            }
            native.progress =
                CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, native.handle, nullptr, native.instance, nullptr);
            if (native.progress == nullptr && controlCreationError == ERROR_SUCCESS)
            {
                controlCreationError = GetLastError();
                if (controlCreationError == ERROR_SUCCESS)
                {
                    controlCreationError = ERROR_FUNCTION_FAILED;
                }
            }
            if (description.cancelable)
            {
                native.cancel = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0,
                    0,
                    0,
                    0,
                    native.handle,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelControlId)),
                    native.instance,
                    nullptr);
                if (native.cancel == nullptr && controlCreationError == ERROR_SUCCESS)
                {
                    controlCreationError = GetLastError();
                    if (controlCreationError == ERROR_SUCCESS)
                    {
                        controlCreationError = ERROR_FUNCTION_FAILED;
                    }
                }
            }
            if (native.heading == nullptr || native.message == nullptr || native.progress == nullptr ||
                (description.cancelable && native.cancel == nullptr))
            {
                return statusFromWin32(
                    IO::Types::ErrorCode::OpenFailed,
                    controlCreationError == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : controlCreationError,
                    "create ProgressDialog controls");
            }

            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            SendMessageW(native.heading, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(native.message, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (native.cancel != nullptr)
            {
                SendMessageW(native.cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            SendMessageW(native.progress, PBM_SETRANGE32, 0, kProgressRange);

            status = applyProgressMode(state, state.mode);
            if (!status.ok())
            {
                rollbackProgressOpenBestEffort(state);
                return status;
            }
            status = layoutProgressWindow(*state.platform, dpi);
            if (!status.ok())
            {
                rollbackProgressOpenBestEffort(state);
                return status;
            }

            if (state.ownerId.isValid() && state.blocksOwner)
            {
#if DESKTOP_INTERNAL_TEST_HOOKS
                if (Detail::consumeFailure(TestHooks::FailurePoint::ProgressOwnerBlocking))
                {
                    rollbackProgressOpenBestEffort(state);
                    return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
                }
#endif
                EnableWindow(state.platform->nativeOwner, FALSE);
                if (IsWindowEnabled(state.platform->nativeOwner) != FALSE)
                {
                    rollbackProgressOpenBestEffort(state);
                    return statusFromWin32(IO::Types::ErrorCode::NativeFailure, ERROR_FUNCTION_FAILED, "EnableWindow ProgressDialog owner");
                }
                state.platform->blockingOwner = true;
            }
            ShowWindow(state.platform->handle, SW_SHOWNOACTIVATE);
            if (UpdateWindow(state.platform->handle) == FALSE)
            {
                status = statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "UpdateWindow ProgressDialog");
                rollbackProgressOpenBestEffort(state);
                return status;
            }
            state.nativeDestroyedPendingFinalize = false;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            rollbackProgressOpenBestEffort(state);
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            rollbackProgressOpenBestEffort(state);
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    ProgressCloseResult closeProgress(ProgressDialogState &state) noexcept
    {
        if (!state.platform)
        {
            return {IO::successStatus(), true};
        }
        if (state.platform->ownerThreadId != GetCurrentThreadId())
        {
            return {IO::makeStatus(IO::Types::ErrorCode::ResourceBusy), false};
        }
        return finalizeNative(state, true);
    }

    bool closeProgressBestEffort(ProgressDialogState &state) noexcept
    {
        if (!state.platform)
        {
            return true;
        }
        if (state.platform->ownerThreadId == GetCurrentThreadId())
        {
            return finalizeNative(state, true).resourceClosed;
        }
        if ((state.platform->handle == nullptr || IsWindow(state.platform->handle) == FALSE) && !state.platform->registered &&
            !state.platform->blockingOwner)
        {
            if (state.platform->classReferenceHeld)
            {
                const IO::Types::Status status = releaseProgressClass();
                if (!status.ok())
                {
                    return false;
                }
                state.platform->classReferenceHeld = false;
            }
            state.platform.reset();
            state.nativeDestroyedPendingFinalize = true;
            state.cancelRequested = false;
            return true;
        }
        return false;
    }

    void finalizeProgressForDispatcherExit(ProgressDialogState &state) noexcept
    {
        const ProgressCloseResult result = finalizeNative(state, false);
        if (!result.resourceClosed)
        {
            abandonNativeForThreadExit(state);
        }
    }

    IO::Types::Status notifyProgressOwnerLoss(WindowState &owner) noexcept
    {
        Dispatcher &current = dispatcher();
        if (!current.progressDialogs)
        {
            return IO::successStatus();
        }
        std::size_t index = 0;
        while (current.progressDialogs && index < current.progressDialogs->size())
        {
            ProgressDialogState *state = (*current.progressDialogs)[index];
            if (state != nullptr && state->ownerId == owner.id)
            {
                ProgressCloseResult result = finalizeNative(*state, false);
                if (!result.resourceClosed)
                {
                    return std::move(result.status);
                }
                continue;
            }
            ++index;
        }
        return IO::successStatus();
    }

    void notifyProgressOwnerLossBestEffort(WindowState &owner) noexcept
    {
        Dispatcher &current = dispatcher();
        if (!current.progressDialogs)
        {
            return;
        }
        std::size_t index = 0;
        while (current.progressDialogs && index < current.progressDialogs->size())
        {
            ProgressDialogState *state = (*current.progressDialogs)[index];
            if (state != nullptr && state->ownerId == owner.id)
            {
                const ProgressCloseResult result = finalizeNative(*state, false);
                if (!result.resourceClosed && state->platform && state->platform->registered)
                {
                    ++index;
                }
                continue;
            }
            ++index;
        }
    }

    bool windowHasBlockingProgressDialog(const WindowState &window) noexcept
    {
        Dispatcher &current = dispatcher();
        if (!current.progressDialogs)
        {
            return false;
        }
        for (const ProgressDialogState *state : *current.progressDialogs)
        {
            if (state != nullptr && state->platform && !state->nativeDestroyedPendingFinalize && state->ownerId == window.id && state->blocksOwner &&
                state->platform->blockingOwner)
            {
                return true;
            }
        }
        return false;
    }

    IO::Types::Status setProgressTitle(ProgressDialogState &state, std::string_view title) noexcept
    {
        return setNativeText(state.platform->handle, title);
    }

    IO::Types::Status setProgressHeading(ProgressDialogState &state, std::string_view heading) noexcept
    {
        return setNativeText(state.platform->heading, heading);
    }

    IO::Types::Status setProgressMessage(ProgressDialogState &state, std::string_view message) noexcept
    {
        return setNativeText(state.platform->message, message);
    }

    IO::Types::Status setProgressMode(ProgressDialogState &state, Types::Dialogs::Progress::Mode mode) noexcept
    {
        IO::Types::Status status = applyProgressMode(state, mode);
        if (!status.ok())
        {
            const IO::Types::Status rollback = applyProgressMode(state, state.mode);
            if (!rollback.ok())
            {
                recordPumpFailure(rollback);
            }
        }
        return status;
    }

    IO::Types::Status setProgressValue(ProgressDialogState &state, double progress) noexcept
    {
#if DESKTOP_INTERNAL_TEST_HOOKS
        if (Detail::consumeFailure(TestHooks::FailurePoint::ProgressMutation))
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
        }
#endif
        if (state.mode == Types::Dialogs::Progress::Mode::Determinate)
        {
            const int position = static_cast<int>(std::lround(progress * kProgressRange));
            SendMessageW(state.platform->progress, PBM_SETPOS, static_cast<WPARAM>(position), 0);
        }
        return IO::successStatus();
    }

#if DESKTOP_INTERNAL_TEST_HOOKS
    namespace
    {
        [[nodiscard]] TestHooks::NativePixelRect nativeRect(HWND window) noexcept
        {
            RECT rect{};
            if (window == nullptr || GetWindowRect(window, &rect) == FALSE)
            {
                return {};
            }
            return {
                .x = rect.left,
                .y = rect.top,
                .width = static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left)),
                .height = static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top))};
        }

        [[nodiscard]] std::wstring nativeText(HWND window)
        {
            const int length = window != nullptr ? GetWindowTextLengthW(window) : 0;
            if (length <= 0)
            {
                return {};
            }
            std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
            const int copied = GetWindowTextW(window, text.data(), length + 1);
            text.resize(static_cast<std::size_t>(std::max(0, copied)));
            return text;
        }

        [[nodiscard]] IO::Types::Status requireTestProgress(const ProgressDialogState *state) noexcept
        {
            if (state == nullptr || !state->platform || state->platform->handle == nullptr)
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }
            return state->platform->ownerThreadId == GetCurrentThreadId() ? IO::successStatus() : IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        }
    } // namespace

    TestHooks::ProgressDialogNativeSnapshot inspectProgressDialog(const ProgressDialogState *state) noexcept
    {
        TestHooks::ProgressDialogNativeSnapshot snapshot;
        if (state == nullptr)
        {
            return snapshot;
        }
        snapshot.nativeDestroyedPendingFinalize = state->nativeDestroyedPendingFinalize;
        if (!state->platform)
        {
            return snapshot;
        }
        try
        {
            const ProgressDialogData &data = *state->platform;
            snapshot.nativeWindow = data.handle != nullptr && IsWindow(data.handle) != FALSE;
            snapshot.cancelControl = data.cancel != nullptr && IsWindow(data.cancel) != FALSE;
            snapshot.registered = data.registered;
            snapshot.classReferenceHeld = data.classReferenceHeld;
            snapshot.blockingOwner = data.blockingOwner;
            snapshot.windowBounds = nativeRect(data.handle);
            snapshot.progressBounds = nativeRect(data.progress);
            snapshot.title = nativeText(data.handle);
            snapshot.heading = nativeText(data.heading);
            snapshot.message = nativeText(data.message);
            if (data.progress != nullptr)
            {
                snapshot.rangeMinimum = static_cast<int>(SendMessageW(data.progress, PBM_GETRANGE, TRUE, 0));
                snapshot.rangeMaximum = static_cast<int>(SendMessageW(data.progress, PBM_GETRANGE, FALSE, 0));
                snapshot.position = static_cast<int>(SendMessageW(data.progress, PBM_GETPOS, 0, 0));
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR style = GetWindowLongPtrW(data.progress, GWL_STYLE);
                if (style != 0 || GetLastError() == ERROR_SUCCESS)
                {
                    snapshot.marquee = (style & PBS_MARQUEE) != 0;
                }
            }
        }
        catch (...)
        {
            return {};
        }
        return snapshot;
    }

    IO::Types::Status requestProgressDialogCancel(ProgressDialogState *state) noexcept
    {
        const IO::Types::Status status = requireTestProgress(state);
        if (!status.ok())
        {
            return status;
        }
        if (state->platform->cancel == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }
        SendMessageW(
            state->platform->handle,
            WM_COMMAND,
            MAKEWPARAM(kCancelControlId, BN_CLICKED),
            reinterpret_cast<LPARAM>(state->platform->cancel));
        return IO::successStatus();
    }

    IO::Types::Status requestProgressDialogClose(ProgressDialogState *state) noexcept
    {
        const IO::Types::Status status = requireTestProgress(state);
        if (!status.ok())
        {
            return status;
        }
        SendMessageW(state->platform->handle, WM_CLOSE, 0, 0);
        return IO::successStatus();
    }

    IO::Types::Status destroyNativeProgressDialog(ProgressDialogState *state) noexcept
    {
        const IO::Types::Status status = requireTestProgress(state);
        if (!status.ok())
        {
            return status;
        }
        return DestroyWindow(state->platform->handle) != FALSE
                   ? IO::successStatus()
                   : statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "DestroyWindow ProgressDialog test hook");
    }

    IO::Types::Status simulateProgressDialogDpiChange(
        ProgressDialogState *state,
        TestHooks::NativePixelRect suggestedBounds,
        std::uint32_t dpi) noexcept
    {
        const IO::Types::Status status = requireTestProgress(state);
        if (!status.ok())
        {
            return status;
        }
        constexpr auto maximumLong = std::numeric_limits<LONG>::max();
        if (dpi == 0 || suggestedBounds.width > static_cast<std::uint32_t>(maximumLong) ||
            suggestedBounds.height > static_cast<std::uint32_t>(maximumLong) ||
            suggestedBounds.x > maximumLong - static_cast<LONG>(suggestedBounds.width) ||
            suggestedBounds.y > maximumLong - static_cast<LONG>(suggestedBounds.height))
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        RECT suggested{
            suggestedBounds.x,
            suggestedBounds.y,
            suggestedBounds.x + static_cast<LONG>(suggestedBounds.width),
            suggestedBounds.y + static_cast<LONG>(suggestedBounds.height)};
        SendMessageW(state->platform->handle, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        return IO::successStatus();
    }

    std::size_t activeProgressDialogCount() noexcept
    {
        Dispatcher &current = dispatcher();
        return current.progressDialogs ? current.progressDialogs->size() : 0;
    }

    std::size_t deferredProgressDialogCount() noexcept
    {
        Dispatcher &current = dispatcher();
        std::scoped_lock lock(current.deferredMutex);
        std::size_t count = 0;
        for (const ProgressDialogState *state = current.deferredProgressDialogCleanupHead.get(); state != nullptr; state = state->deferredCleanupNext)
        {
            ++count;
        }
        return count;
    }

    std::size_t progressDialogClassReferenceCount() noexcept
    {
        std::scoped_lock lock(progressClassMutex);
        return progressClassUsers;
    }
#endif
} // namespace GameWIP::Desktop::Detail::Platform
