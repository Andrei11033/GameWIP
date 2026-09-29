/// @file win32_shell.cpp
/// @brief Win32 shell-integration backend entry points.

#include "desktop/internal/shell_platform.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <winreg.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    // ------------------------------------------------------------
    // COM, icon, and taskbar helpers
    // ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;

        std::atomic_uint64_t nextRegistryBackupIdentity{1};

        /// @brief Owns one COM interface pointer and releases it at scope exit.
        template <typename Interface> class ComPtr final
        {
        public:
            ComPtr() noexcept = default;
            ComPtr(const ComPtr &) = delete;
            ComPtr &operator=(const ComPtr &) = delete;
            ~ComPtr() noexcept
            {
                reset();
            }
            [[nodiscard]] Interface *get() const noexcept
            {
                return value_;
            }
            [[nodiscard]] Interface **put() noexcept
            {
                reset();
                return &value_;
            }
            Interface *operator->() const noexcept
            {
                return value_;
            }
            explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }
            void reset() noexcept
            {
                if (value_ != nullptr)
                {
                    value_->Release();
                    value_ = nullptr;
                }
            }

        private:
            Interface *value_ = nullptr;
        };

        /// @brief Maps common COM outcomes to the portable shell status vocabulary.
        [[nodiscard]] IO::Types::Status statusFromHResult(HRESULT result) noexcept
        {
            ErrorCode code = ErrorCode::NativeFailure;
            if (result == E_OUTOFMEMORY)
            {
                code = ErrorCode::OutOfMemory;
            }
            else if (result == E_INVALIDARG)
            {
                code = ErrorCode::InvalidArgument;
            }
            else if (result == E_ACCESSDENIED)
            {
                code = ErrorCode::PermissionDenied;
            }
            else if (result == RPC_E_CHANGED_MODE)
            {
                code = ErrorCode::ResourceBusy;
            }
            else if (result == E_NOTIMPL || result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
            {
                code = ErrorCode::Unsupported;
            }
            return IO::makeStatus(code, static_cast<std::int64_t>(result));
        }

        /// @brief Ensures the calling thread has a compatible COM apartment for shell APIs.
        class ApartmentLease final
        {
        public:
            ApartmentLease() noexcept
            {
                APTTYPE type = APTTYPE_CURRENT;
                APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
                HRESULT result = CoGetApartmentType(&type, &qualifier);
                if (SUCCEEDED(result))
                {
                    if (type == APTTYPE_STA || type == APTTYPE_MAINSTA)
                    {
                        status_ = IO::successStatus();
                    }
                    else
                    {
                        status_ = IO::makeStatus(ErrorCode::ResourceBusy, static_cast<std::int64_t>(RPC_E_CHANGED_MODE));
                    }
                    return;
                }
                if (result != CO_E_NOTINITIALIZED)
                {
                    status_ = statusFromHResult(result);
                    return;
                }
                result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                if (SUCCEEDED(result))
                {
                    ownsInitialization_ = true;
                    status_ = IO::successStatus();
                }
                else
                {
                    status_ = statusFromHResult(result);
                }
            }
            ~ApartmentLease() noexcept
            {
                if (ownsInitialization_)
                {
                    CoUninitialize();
                }
            }
            [[nodiscard]] const IO::Types::Status &status() const noexcept
            {
                return status_;
            }

        private:
            IO::Types::Status status_;
            bool ownsInitialization_ = false;
        };

        /// @brief Copies validated packed RGBA8 pixels into a native BGRA8 buffer.
        void copyRgbaToBgra(std::span<const std::byte> source, std::byte *destination) noexcept
        {
            for (std::size_t offset = 0; offset < source.size(); offset += 4)
            {
                const std::array<std::byte, 4> pixel{source[offset + 2], source[offset + 1], source[offset], source[offset + 3]};
                destination = std::copy(pixel.begin(), pixel.end(), destination);
            }
        }

        /// @brief Converts retained RGBA8 icon pixels into a temporary native HICON.
        [[nodiscard]] HICON createIcon(const OwnedIconImage &image) noexcept
        {
            if (image.size.width == 0 || image.size.height == 0 || image.rgba8.empty())
            {
                return nullptr;
            }
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = static_cast<LONG>(image.size.width);
            header.bV5Height = -static_cast<LONG>(image.size.height);
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            HDC screen = GetDC(nullptr);
            if (screen == nullptr)
            {
                return nullptr;
            }
            void *pixels = nullptr;
            HBITMAP color = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO *>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);
            if (color == nullptr || pixels == nullptr)
            {
                if (color != nullptr)
                {
                    DeleteObject(color);
                }
                return nullptr;
            }
            copyRgbaToBgra(image.rgba8, static_cast<std::byte *>(pixels));
            HBITMAP mask = CreateBitmap(static_cast<int>(image.size.width), static_cast<int>(image.size.height), 1, 1, nullptr);
            if (mask == nullptr)
            {
                DeleteObject(color);
                return nullptr;
            }
            ICONINFO iconInfo{TRUE, 0, 0, mask, color};
            HICON icon = CreateIconIndirect(&iconInfo);
            DeleteObject(mask);
            DeleteObject(color);
            return icon;
        }

        /// @brief Selects the retained icon whose dimensions are nearest to a taskbar request.
        [[nodiscard]] const OwnedIconImage &closestIcon(std::span<const OwnedIconImage> images, int desiredWidth, int desiredHeight) noexcept
        {
            return *std::min_element(
                images.begin(),
                images.end(),
                [desiredWidth, desiredHeight](const OwnedIconImage &left, const OwnedIconImage &right)
                {
                    const auto distance = [desiredWidth, desiredHeight](const OwnedIconImage &image)
                    {
                        return std::llabs(static_cast<long long>(image.size.width) - desiredWidth) +
                               std::llabs(static_cast<long long>(image.size.height) - desiredHeight);
                    };
                    return distance(left) < distance(right);
                });
        }

        /// @brief Owns native taskbar resources associated with one portable TaskbarItem.
        struct TaskbarNative
        {
            HWND window = nullptr;
            HICON overlay = nullptr;
            std::vector<HICON> buttonIcons;
            bool buttonsInitialized = false;
        };

        constexpr std::size_t kTaskbarButtonCapacity = 7;

        /// @brief Converts and bounds UTF-8 tray text for NOTIFYICONDATAW.
        [[nodiscard]] IO::Types::Status convertTip(std::string_view text, std::array<wchar_t, 260> &output)
        {
            std::wstring converted;
            DWORD nativeCode = ERROR_SUCCESS;
            if (!utf8ToUtf16(text, converted, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            if (converted.size() >= output.size())
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            std::copy(converted.begin(), converted.end(), output.begin());
            output[converted.size()] = L'\0';
            return IO::successStatus();
        }

        /// @brief Applies taskbar progress state through the COM taskbar interface.
        [[nodiscard]] IO::Types::Status applyTaskbarProgress(TaskbarNative &native, const std::optional<Types::Shell::Progress> &progress) noexcept
        {
            ApartmentLease apartment;
            if (!apartment.status().ok())
            {
                return apartment.status();
            }
            ComPtr<ITaskbarList3> taskbar;
            HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            result = taskbar->HrInit();
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            TBPFLAG flag = TBPF_NOPROGRESS;
            if (progress.has_value())
            {
                switch (progress->state)
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
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
            }
            result = taskbar->SetProgressState(native.window, flag);
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            if (progress.has_value() && progress->state != Types::Shell::ProgressState::Indeterminate)
            {
                const ULONGLONG value = static_cast<ULONGLONG>(std::llround(std::clamp(progress->fraction, 0.0, 1.0) * 1000.0));
                result = taskbar->SetProgressValue(native.window, value, 1000);
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }
            return IO::successStatus();
        }

        /// @brief Rebuilds taskbar thumbnail buttons while retaining icon cleanup on failure.
        [[nodiscard]] IO::Types::Status applyTaskbarButtons(
            TaskbarNative &native,
            std::span<const OwnedThumbnailButton> buttons,
            std::vector<HICON> &newIcons) noexcept
        {
            if (buttons.size() > kTaskbarButtonCapacity)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            const auto cleanupIcons = [&]() noexcept
            {
                for (HICON icon : newIcons)
                {
                    if (icon != nullptr)
                    {
                        DestroyIcon(icon);
                    }
                }
                newIcons.clear();
            };
            try
            {
                ApartmentLease apartment;
                if (!apartment.status().ok())
                {
                    return apartment.status();
                }
                ComPtr<ITaskbarList3> taskbar;
                HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()));
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
                result = taskbar->HrInit();
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }

                std::array<THUMBBUTTON, kTaskbarButtonCapacity> nativeButtons;
                newIcons.assign(kTaskbarButtonCapacity, nullptr);
                for (std::size_t index = 0; index < kTaskbarButtonCapacity; ++index)
                {
                    THUMBBUTTON &nativeButton = nativeButtons[index];
                    nativeButton.dwMask = THB_FLAGS | THB_TOOLTIP;
                    nativeButton.iId = 0x80000000U | static_cast<UINT>(index);
                    nativeButton.iBitmap = 0;
                    nativeButton.hIcon = nullptr;
                    nativeButton.szTip[0] = L'\0';
                    nativeButton.dwFlags = THBF_HIDDEN;
                    if (index >= buttons.size())
                    {
                        continue;
                    }
                    const OwnedThumbnailButton &button = buttons[index];
                    nativeButton.iId = button.id.value;
                    nativeButton.dwFlags = button.enabled ? THBF_ENABLED : THBF_DISABLED;
                    IO::Types::Status tipStatus = convertTip(button.label, *reinterpret_cast<std::array<wchar_t, 260> *>(nativeButton.szTip));
                    if (!tipStatus.ok())
                    {
                        cleanupIcons();
                        return tipStatus;
                    }
                    if (button.icon.has_value())
                    {
                        HICON icon = createIcon(*button.icon);
                        if (icon == nullptr)
                        {
                            cleanupIcons();
                            return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "create taskbar thumbnail icon");
                        }
                        newIcons[index] = icon;
                        nativeButton.dwMask |= THB_ICON;
                        nativeButton.hIcon = icon;
                    }
                }

                if (!native.buttonsInitialized)
                {
                    result = taskbar->ThumbBarAddButtons(native.window, static_cast<UINT>(nativeButtons.size()), nativeButtons.data());
                }
                else
                {
                    result = taskbar->ThumbBarUpdateButtons(native.window, static_cast<UINT>(nativeButtons.size()), nativeButtons.data());
                }
                if (FAILED(result))
                {
                    cleanupIcons();
                    return statusFromHResult(result);
                }
                native.buttonsInitialized = true;
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                cleanupIcons();
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                cleanupIcons();
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        /// @brief Owns the hidden message HWND and icon used by one tray resource.
        struct TrayNative
        {
            TrayIconState *state = nullptr;
            HWND window = nullptr;
            HICON icon = nullptr;
            UINT taskbarCreatedMessage = 0;
        };

        inline constexpr wchar_t kTrayWindowClassName[] = L"GameWIP.Shell.TrayMessage";
        inline constexpr UINT kTrayCallbackMessage = WM_APP + 0x271;

        /// @brief Converts retained RGBA8 pixels into a menu-compatible DIB bitmap.
        [[nodiscard]] HBITMAP createMenuBitmap(const OwnedIconImage &image) noexcept
        {
            if (image.size.width == 0 || image.size.height == 0 || image.rgba8.empty())
            {
                return nullptr;
            }
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = static_cast<LONG>(image.size.width);
            header.bV5Height = -static_cast<LONG>(image.size.height);
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            HDC screen = GetDC(nullptr);
            if (screen == nullptr)
            {
                return nullptr;
            }
            void *pixels = nullptr;
            HBITMAP bitmap = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO *>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);
            if (bitmap == nullptr || pixels == nullptr)
            {
                if (bitmap != nullptr)
                {
                    DeleteObject(bitmap);
                }
                return nullptr;
            }
            copyRgbaToBgra(image.rgba8, static_cast<std::byte *>(pixels));
            return bitmap;
        }

        /// @brief Builds one tray menu label with accelerator and badge suffixes.
        [[nodiscard]] bool menuText(const OwnedTrayMenuItem &item, std::wstring &text)
        {
            std::string utf8 = item.label;
            if (!item.accelerator.empty())
            {
                utf8 += "\t";
                utf8 += item.accelerator;
            }
            if (!item.badge.empty())
            {
                utf8 += " [";
                utf8 += item.badge;
                utf8 += "]";
            }
            DWORD nativeCode = ERROR_SUCCESS;
            return utf8ToUtf16(utf8, text, nativeCode);
        }

        /// @brief Recursively creates native tray menu items and records bitmap ownership.
        [[nodiscard]] bool appendMenuItems(HMENU menu, const std::vector<OwnedTrayMenuItem> &items, std::vector<HBITMAP> &bitmaps)
        {
            for (const OwnedTrayMenuItem &item : items)
            {
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);
                if (item.kind == Types::Tray::MenuItemKind::Separator)
                {
                    info.fMask = MIIM_FTYPE;
                    info.fType = MFT_SEPARATOR;
                    if (InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &info) == FALSE)
                    {
                        return false;
                    }
                    continue;
                }
                std::wstring text;
                if (!menuText(item, text))
                {
                    return false;
                }
                info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
                info.fType = MFT_STRING;
                info.fState = item.enabled ? MFS_ENABLED : MFS_DISABLED;
                if ((item.kind == Types::Tray::MenuItemKind::Check || item.kind == Types::Tray::MenuItemKind::Radio) && item.checked)
                {
                    info.fState |= MFS_CHECKED;
                }
                if (item.kind == Types::Tray::MenuItemKind::Radio)
                {
                    info.fType |= MFT_RADIOCHECK;
                }
                info.dwTypeData = text.data();
                HMENU submenu = nullptr;
                if (item.kind == Types::Tray::MenuItemKind::Submenu)
                {
                    submenu = CreatePopupMenu();
                    if (submenu == nullptr)
                    {
                        return false;
                    }
                    try
                    {
                        if (!appendMenuItems(submenu, item.children, bitmaps))
                        {
                            DestroyMenu(submenu);
                            return false;
                        }
                    }
                    catch (...)
                    {
                        DestroyMenu(submenu);
                        throw;
                    }
                    info.fMask |= MIIM_SUBMENU;
                    info.hSubMenu = submenu;
                }
                else
                {
                    info.fMask |= MIIM_ID;
                    info.wID = item.commandId.value;
                }
                if (item.icon.has_value())
                {
                    HBITMAP bitmap = createMenuBitmap(*item.icon);
                    if (bitmap == nullptr)
                    {
                        return false;
                    }
                    try
                    {
                        bitmaps.push_back(bitmap);
                    }
                    catch (...)
                    {
                        DeleteObject(bitmap);
                        throw;
                    }
                    info.fMask |= MIIM_BITMAP;
                    info.hbmpItem = bitmap;
                }
                if (InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &info) == FALSE)
                {
                    if (submenu != nullptr)
                    {
                        DestroyMenu(submenu);
                    }
                    return false;
                }
            }
            return true;
        }

        /// @brief Releases temporary menu bitmaps after the popup menu is dismissed.
        void destroyMenuBitmaps(std::vector<HBITMAP> &bitmaps) noexcept
        {
            for (HBITMAP bitmap : bitmaps)
            {
                if (bitmap != nullptr)
                {
                    DeleteObject(bitmap);
                }
            }
            bitmaps.clear();
        }

        /// @brief Publishes a tray activation with the best-effort cursor screen position.
        void publishTrayActivation(TrayNative &native, Types::Shell::Events::TrayActivationKind kind) noexcept
        {
            if (native.state == nullptr || native.state->eventQueue == nullptr)
            {
                return;
            }
            POINT point{};
            std::optional<Types::ScreenPosition> position;
            if (GetCursorPos(&point) != FALSE)
            {
                position = Types::ScreenPosition{point.x, point.y};
            }
            publishShellEvent(*native.state->eventQueue, Types::Shell::Events::TrayActivated{native.state->id, kind, position});
        }

        /// @brief Builds, shows, and tears down the owner-thread tray popup menu.
        void showTrayMenu(TrayNative &native) noexcept
        {
            if (native.state == nullptr || native.state->eventQueue == nullptr || native.state->menu.empty())
            {
                return;
            }
            HMENU menu = nullptr;
            std::vector<HBITMAP> bitmaps;
            try
            {
                menu = CreatePopupMenu();
                if (menu == nullptr)
                {
                    return;
                }
                if (!appendMenuItems(menu, native.state->menu, bitmaps))
                {
                    destroyMenuBitmaps(bitmaps);
                    DestroyMenu(menu);
                    return;
                }
                POINT point{};
                if (GetCursorPos(&point) == FALSE)
                {
                    destroyMenuBitmaps(bitmaps);
                    DestroyMenu(menu);
                    return;
                }
                SetForegroundWindow(native.window);
                const UINT command =
                    static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, native.window, nullptr));
                PostMessageW(native.window, WM_NULL, 0, 0);
                destroyMenuBitmaps(bitmaps);
                DestroyMenu(menu);
                menu = nullptr;
                if (command != 0)
                {
                    publishShellEvent(
                        *native.state->eventQueue,
                        Types::Shell::Events::TrayCommandInvoked{native.state->id, Types::Shell::CommandId{command}});
                }
            }
            catch (...)
            {
                destroyMenuBitmaps(bitmaps);
                if (menu != nullptr)
                {
                    DestroyMenu(menu);
                }
            }
        }

        /// @brief Routes tray recreation, activation, and command messages.
        LRESULT CALLBACK trayWindowProc(HWND window, UINT message, WPARAM, LPARAM lParam)
        {
            auto *native = reinterpret_cast<TrayNative *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
                native = static_cast<TrayNative *>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(native));
                if (native != nullptr)
                {
                    native->window = window;
                }
            }
            if (native == nullptr)
            {
                return DefWindowProcW(window, message, 0, lParam);
            }
            if (message == native->taskbarCreatedMessage)
            {
                try
                {
                    NOTIFYICONDATAW data{};
                    data.cbSize = sizeof(data);
                    data.hWnd = window;
                    data.uID = static_cast<UINT>(native->state->id.value);
                    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
                    data.uCallbackMessage = kTrayCallbackMessage;
                    data.hIcon = native->icon;
                    DWORD nativeCode = ERROR_SUCCESS;
                    std::wstring tooltip;
                    if (utf8ToUtf16(native->state->tooltip, tooltip, nativeCode))
                    {
                        const std::size_t length = std::min<std::size_t>(tooltip.size(), ARRAYSIZE(data.szTip) - 1);
                        std::copy_n(tooltip.data(), length, data.szTip);
                    }
                    static_cast<void>(Shell_NotifyIconW(NIM_ADD, &data));
                }
                catch (...)
                {
                    SetLastError(ERROR_GEN_FAILURE);
                }
                return 0;
            }
            if (message == kTrayCallbackMessage)
            {
                switch (static_cast<UINT>(lParam))
                {
                case WM_LBUTTONUP:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::Primary);
                    return 0;
                case WM_LBUTTONDBLCLK:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::DoublePrimary);
                    return 0;
                case WM_RBUTTONUP:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::Secondary);
                    showTrayMenu(*native);
                    return 0;
                case NIN_SELECT:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::Primary);
                    return 0;
                default:
                    break;
                }
            }
            return DefWindowProcW(window, message, 0, lParam);
        }

        [[nodiscard]] IO::Types::Status ensureTrayWindowClass() noexcept
        {
            WNDCLASSEXW existing{};
            existing.cbSize = sizeof(existing);
            HINSTANCE instance = GetModuleHandleW(nullptr);
            if (GetClassInfoExW(instance, kTrayWindowClassName, &existing) != FALSE)
            {
                return IO::successStatus();
            }
            WNDCLASSEXW classInfo{};
            classInfo.cbSize = sizeof(classInfo);
            classInfo.lpfnWndProc = trayWindowProc;
            classInfo.hInstance = instance;
            classInfo.lpszClassName = kTrayWindowClassName;
            if (RegisterClassExW(&classInfo) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "RegisterClassExW tray window");
            }
            return IO::successStatus();
        }

        /// @brief Owns the hidden message HWND used for one notification center.
        struct NotificationNative
        {
            NotificationCenterState *state = nullptr;
            HWND window = nullptr;
        };

        inline constexpr wchar_t kNotificationWindowClassName[] = L"GameWIP.Shell.NotificationMessage";

        [[nodiscard]] IO::Types::Status ensureNotificationWindowClass() noexcept;

        /// @brief Rejects notification features unsupported by the Win32 balloon fallback.
        [[nodiscard]] IO::Types::Status validateBasicNotification(
            const OwnedNotificationDescription &description,
            std::wstring &title,
            std::wstring &body,
            DWORD &nativeCode)
        {
            if (!description.actions.empty() || !description.inputs.empty() || !description.media.empty() || description.schedule.has_value() ||
                !description.group.empty() || !description.badge.empty() || description.progress.has_value() ||
                description.urgency != Types::Notifications::Urgency::Normal)
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }
            if (!utf8ToUtf16(description.title, title, nativeCode) || !utf8ToUtf16(description.body, body, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            if (title.size() >= ARRAYSIZE(NOTIFYICONDATAW{}.szInfoTitle) || body.size() >= ARRAYSIZE(NOTIFYICONDATAW{}.szInfo))
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            return IO::successStatus();
        }

        LRESULT CALLBACK notificationWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *native = reinterpret_cast<NotificationNative *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
                native = static_cast<NotificationNative *>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(native));
                if (native != nullptr)
                {
                    native->window = window;
                }
            }
            if (native == nullptr || native->state == nullptr)
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }
            if (message == kTrayCallbackMessage)
            {
                const std::uint64_t id = static_cast<UINT>(wParam);
                if (native->state->eventQueue == nullptr)
                {
                    return 0;
                }
                if (lParam == NIN_BALLOONUSERCLICK)
                {
                    publishShellEvent(
                        *native->state->eventQueue,
                        Types::Shell::Events::NotificationActivated{Types::Shell::NotificationId{id}, std::nullopt, {}});
                }
                else if (lParam == NIN_BALLOONTIMEOUT)
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

        [[nodiscard]] IO::Types::Status ensureNotificationWindowClass() noexcept
        {
            WNDCLASSEXW existing{};
            existing.cbSize = sizeof(existing);
            HINSTANCE instance = GetModuleHandleW(nullptr);
            if (GetClassInfoExW(instance, kNotificationWindowClassName, &existing) != FALSE)
            {
                return IO::successStatus();
            }
            WNDCLASSEXW classInfo{};
            classInfo.cbSize = sizeof(classInfo);
            classInfo.lpfnWndProc = notificationWindowProc;
            classInfo.hInstance = instance;
            classInfo.lpszClassName = kNotificationWindowClassName;
            if (RegisterClassExW(&classInfo) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "RegisterClassExW notification window");
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status showBasicNotification(
            NotificationNative &native,
            std::uint64_t id,
            const OwnedNotificationDescription &description,
            bool add)
        {
            std::wstring title;
            std::wstring body;
            DWORD nativeCode = ERROR_SUCCESS;
            IO::Types::Status status = validateBasicNotification(description, title, body, nativeCode);
            if (!status.ok())
            {
                return status;
            }
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
            if (add)
            {
                data.dwState = NIS_HIDDEN;
                data.dwStateMask = NIS_HIDDEN;
            }
            if (Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data) == FALSE)
            {
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "Shell_NotifyIconW notification");
            }
            return IO::successStatus();
        }

        /// @brief Appends one Windows command-line argument using native quoting rules.
        void appendQuotedArgument(std::wstring_view argument, std::wstring &commandLine)
        {
            commandLine.push_back(L'"');
            std::size_t backslashes = 0;
            for (wchar_t character : argument)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (character == L'"')
                {
                    commandLine.append(backslashes * 2 + 1, L'\\');
                    commandLine.push_back(L'"');
                    backslashes = 0;
                    continue;
                }
                commandLine.append(backslashes, L'\\');
                backslashes = 0;
                commandLine.push_back(character);
            }
            commandLine.append(backslashes * 2, L'\\');
            commandLine.push_back(L'"');
        }

        [[nodiscard]] IO::Types::Status launchArgumentText(
            const Types::Shell::LaunchArgumentView &argument,
            std::wstring &text,
            bool allowPlaceholders)
        {
            DWORD nativeCode = ERROR_SUCCESS;
            if (const auto *literal = std::get_if<Types::Shell::LiteralLaunchArgumentView>(&argument))
            {
                if (!utf8ToUtf16(literal->text, text, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }
                return IO::successStatus();
            }
            if (const auto *target = std::get_if<Types::Shell::TargetLaunchArgumentView>(&argument))
            {
                if (const auto *path = std::get_if<Types::Shell::PathTargetView>(&target->target))
                {
                    text = path->path.get().wstring();
                    return IO::successStatus();
                }
                if (const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target->target))
                {
                    if (!utf8ToUtf16(uri->uri, text, nativeCode))
                    {
                        return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    }
                    return IO::successStatus();
                }
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            if (!allowPlaceholders)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            switch (std::get<Types::Shell::LaunchPlaceholder>(argument))
            {
            case Types::Shell::LaunchPlaceholder::SelectedPath:
            case Types::Shell::LaunchPlaceholder::SelectedUri:
                text = L"%1";
                return IO::successStatus();
            case Types::Shell::LaunchPlaceholder::AllPaths:
            case Types::Shell::LaunchPlaceholder::AllUris:
                text = L"%*";
                return IO::successStatus();
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
        }

        [[nodiscard]] IO::Types::Status makeLaunchCommand(
            const Types::Shell::LaunchActionView &action,
            bool allowPlaceholders,
            std::wstring &executable,
            std::wstring &arguments)
        {
            executable = action.executable.path.get().wstring();
            if (executable.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            arguments.clear();
            for (const Types::Shell::LaunchArgumentView &argument : action.arguments)
            {
                std::wstring text;
                IO::Types::Status status = launchArgumentText(argument, text, allowPlaceholders);
                if (!status.ok())
                {
                    return status;
                }
                if (!arguments.empty())
                {
                    arguments.push_back(L' ');
                }
                if (text == L"%1" || text == L"%*")
                {
                    arguments += text;
                }
                else
                {
                    appendQuotedArgument(text, arguments);
                }
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status createJumpLink(const Types::JumpLists::Task &task, ComPtr<IShellLinkW> &out)
        {
            HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(out.put()));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            std::wstring executable;
            std::wstring arguments;
            IO::Types::Status status = makeLaunchCommand(task.action, false, executable, arguments);
            if (!status.ok())
            {
                return status;
            }
            result = out->SetPath(executable.c_str());
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            result = out->SetArguments(arguments.c_str());
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            std::wstring title;
            DWORD nativeCode = ERROR_SUCCESS;
            if (!utf8ToUtf16(task.title, title, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            result = out->SetDescription(title.c_str());
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status appendJumpTasks(
            ICustomDestinationList *list,
            std::wstring_view category,
            std::span<const Types::JumpLists::Task> tasks)
        {
            ComPtr<IObjectCollection> collection;
            HRESULT result = CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(collection.put()));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            for (const Types::JumpLists::Task &task : tasks)
            {
                ComPtr<IShellLinkW> link;
                IO::Types::Status status = createJumpLink(task, link);
                if (!status.ok())
                {
                    return status;
                }
                result = collection->AddObject(link.get());
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }
            ComPtr<IObjectArray> objects;
            result = collection->QueryInterface(IID_PPV_ARGS(objects.put()));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            std::wstring label(category);
            result = list->AppendCategory(label.c_str(), objects.get());
            return FAILED(result) ? statusFromHResult(result) : IO::successStatus();
        }

        /// @brief Commits or cancels one ICustomDestinationList transaction safely.
        class JumpListTransaction final
        {
        public:
            explicit JumpListTransaction(ICustomDestinationList &list) noexcept
                : list_(&list)
            {
            }
            JumpListTransaction(const JumpListTransaction &) = delete;
            JumpListTransaction &operator=(const JumpListTransaction &) = delete;
            ~JumpListTransaction() noexcept
            {
                if (!committed_)
                {
                    static_cast<void>(list_->AbortList());
                }
            }

            void commit() noexcept
            {
                committed_ = true;
            }

        private:
            ICustomDestinationList *list_ = nullptr;
            bool committed_ = false;
        };

        /// @brief Owns one Win32 registry key handle and its close operation.
        class RegistryKey final
        {
        public:
            RegistryKey() noexcept = default;
            RegistryKey(const RegistryKey &) = delete;
            RegistryKey &operator=(const RegistryKey &) = delete;
            ~RegistryKey() noexcept
            {
                reset();
            }
            HKEY *put() noexcept
            {
                if (value_ != nullptr)
                {
                    RegCloseKey(value_);
                    value_ = nullptr;
                }
                return &value_;
            }
            [[nodiscard]] HKEY get() const noexcept
            {
                return value_;
            }
            void reset() noexcept
            {
                if (value_ != nullptr)
                {
                    RegCloseKey(value_);
                    value_ = nullptr;
                }
            }
            explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }

        private:
            HKEY value_ = nullptr;
        };

        [[nodiscard]] IO::Types::Status registryStatus(LONG code, std::string_view operation)
        {
            ErrorCode portable = ErrorCode::NativeFailure;
            if (code == ERROR_FILE_NOT_FOUND)
            {
                portable = ErrorCode::NotFound;
            }
            else if (code == ERROR_ACCESS_DENIED)
            {
                portable = ErrorCode::PermissionDenied;
            }
            else if (code == ERROR_ALREADY_EXISTS)
            {
                portable = ErrorCode::AlreadyExists;
            }
            else if (code == ERROR_OUTOFMEMORY)
            {
                portable = ErrorCode::OutOfMemory;
            }
            std::string message(operation);
            return IO::makeStatus(portable, code, std::move(message));
        }

        [[nodiscard]] IO::Types::Status openRegistryKey(HKEY root, std::wstring_view path, REGSAM access, RegistryKey &key, bool create)
        {
            LONG result = ERROR_SUCCESS;
            if (create)
            {
                DWORD disposition = 0;
                result =
                    RegCreateKeyExW(root, std::wstring(path).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, key.put(), &disposition);
            }
            else
            {
                result = RegOpenKeyExW(root, std::wstring(path).c_str(), 0, access, key.put());
            }
            return result == ERROR_SUCCESS ? IO::successStatus() : registryStatus(result, "registry key operation");
        }

        [[nodiscard]] IO::Types::Status readRegistryString(HKEY key, std::wstring_view name, std::wstring &value)
        {
            DWORD type = 0;
            DWORD size = 0;
            LONG result = RegGetValueW(key, nullptr, std::wstring(name).c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &size);
            if (result != ERROR_SUCCESS)
            {
                return registryStatus(result, "read registry value");
            }
            try
            {
                value.resize(size / sizeof(wchar_t));
                result = RegGetValueW(key, nullptr, std::wstring(name).c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, value.data(), &size);
                if (result != ERROR_SUCCESS)
                {
                    return registryStatus(result, "read registry value");
                }
                if (!value.empty() && value.back() == L'\0')
                {
                    value.pop_back();
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        /// @brief Captures owned registry state so failed shell registration can roll back.
        class RegistryTreeBackup final
        {
        public:
            RegistryTreeBackup(std::wstring targetPath, std::wstring programPath)
                : targetPath_(std::move(targetPath))
                , programPath_(std::move(programPath))
            {
                const std::uint64_t identity = nextRegistryBackupIdentity.fetch_add(1, std::memory_order_relaxed);
                backupPath_ = L"Software\\GameWIP\\ShellRollback." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(identity);
            }
            RegistryTreeBackup(const RegistryTreeBackup &) = delete;
            RegistryTreeBackup &operator=(const RegistryTreeBackup &) = delete;
            ~RegistryTreeBackup() noexcept
            {
                if (active_)
                {
                    rollbackBestEffort();
                }
                else
                {
                    cleanup();
                }
            }

            [[nodiscard]] IO::Types::Status capture()
            {
                IO::Types::Status status = openRegistryKey(HKEY_CURRENT_USER, backupPath_, KEY_READ | KEY_WRITE, backupRoot_, true);
                if (!status.ok())
                {
                    return status;
                }
                status = captureTree(targetPath_, L"Target", targetCaptured_);
                if (!status.ok())
                {
                    return status;
                }
                status = captureTree(programPath_, L"Program", programCaptured_);
                if (!status.ok())
                {
                    return status;
                }
                active_ = true;
                return IO::successStatus();
            }

            void commit() noexcept
            {
                active_ = false;
                cleanup();
            }

        private:
            [[nodiscard]] IO::Types::Status captureTree(std::wstring_view sourcePath, std::wstring_view slot, bool &captured)
            {
                RegistryKey source;
                const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(sourcePath).c_str(), 0, KEY_READ, source.put());
                if (openResult == ERROR_FILE_NOT_FOUND)
                {
                    captured = false;
                    return IO::successStatus();
                }
                if (openResult != ERROR_SUCCESS)
                {
                    return registryStatus(openResult, "open registry backup source");
                }
                RegistryKey destination;
                IO::Types::Status status = openRegistryKey(backupRoot_.get(), slot, KEY_READ | KEY_WRITE, destination, true);
                if (!status.ok())
                {
                    return status;
                }
                const LONG copyResult = RegCopyTreeW(source.get(), nullptr, destination.get());
                if (copyResult != ERROR_SUCCESS)
                {
                    return registryStatus(copyResult, "copy registry backup source");
                }
                captured = true;
                return IO::successStatus();
            }

            [[nodiscard]] IO::Types::Status restoreTree(std::wstring_view destinationPath, std::wstring_view slot, bool captured)
            {
                const LONG deleteResult = RegDeleteTreeW(HKEY_CURRENT_USER, std::wstring(destinationPath).c_str());
                if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND)
                {
                    return registryStatus(deleteResult, "remove registry replacement");
                }
                if (!captured)
                {
                    return IO::successStatus();
                }
                RegistryKey source;
                IO::Types::Status status = openRegistryKey(backupRoot_.get(), slot, KEY_READ, source, false);
                if (!status.ok())
                {
                    return status;
                }
                RegistryKey destination;
                status = openRegistryKey(HKEY_CURRENT_USER, destinationPath, KEY_READ | KEY_WRITE, destination, true);
                if (!status.ok())
                {
                    return status;
                }
                const LONG copyResult = RegCopyTreeW(source.get(), nullptr, destination.get());
                return copyResult == ERROR_SUCCESS ? IO::successStatus() : registryStatus(copyResult, "restore registry replacement");
            }

            void rollbackBestEffort() noexcept
            {
                try
                {
                    static_cast<void>(restoreTree(programPath_, L"Program", programCaptured_));
                    static_cast<void>(restoreTree(targetPath_, L"Target", targetCaptured_));
                }
                catch (...)
                {
                    active_ = false;
                }
                active_ = false;
                cleanup();
            }

            void cleanup() noexcept
            {
                backupRoot_.reset();
                static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, backupPath_.c_str()));
            }

            std::wstring targetPath_;
            std::wstring programPath_;
            std::wstring backupPath_;
            RegistryKey backupRoot_;
            bool targetCaptured_ = false;
            bool programCaptured_ = false;
            bool active_ = false;
        };

        [[nodiscard]] IO::Types::Status writeRegistryString(HKEY key, std::wstring_view name, std::wstring_view value)
        {
            const std::wstring nameCopy(name);
            const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            const LONG result =
                RegSetValueExW(key, nameCopy.empty() ? nullptr : nameCopy.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(value.data()), bytes);
            return result == ERROR_SUCCESS ? IO::successStatus() : registryStatus(result, "write registry value");
        }

        [[nodiscard]] IO::Types::Status normalizeTarget(
            std::string_view input,
            bool extension,
            std::string &normalizedUtf8,
            std::wstring &normalizedUtf16) noexcept
        {
            try
            {
                normalizedUtf8.assign(input);
                if (extension && normalizedUtf8.starts_with("*"))
                {
                    normalizedUtf8.erase(0, 1);
                }
                if (extension && !normalizedUtf8.starts_with("."))
                {
                    normalizedUtf8.insert(normalizedUtf8.begin(), '.');
                }
                if (!extension && normalizedUtf8.ends_with(':'))
                {
                    normalizedUtf8.pop_back();
                }
                if (normalizedUtf8.empty() || normalizedUtf8.find_first_of("\\/:*?\"<>|\0") != std::string::npos)
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                const std::size_t start = extension ? 1U : 0U;
                if (!extension && (start >= normalizedUtf8.size() || ((normalizedUtf8[start] < 'A' || normalizedUtf8[start] > 'Z') &&
                                                                      (normalizedUtf8[start] < 'a' || normalizedUtf8[start] > 'z'))))
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                for (char &character : normalizedUtf8)
                {
                    if (static_cast<unsigned char>(character) >= 0x80U)
                    {
                        return IO::makeStatus(ErrorCode::InvalidArgument);
                    }
                    if (character >= 'A' && character <= 'Z')
                    {
                        character = static_cast<char>(character - 'A' + 'a');
                    }
                }
                DWORD nativeCode = ERROR_SUCCESS;
                if (!utf8ToUtf16(normalizedUtf8, normalizedUtf16, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] std::wstring registrationProgId(bool extension, std::string_view ownerKey, std::string_view normalizedTarget)
        {
            std::wstring result = extension ? L"GameWIP.File." : L"GameWIP.Uri.";
            DWORD nativeCode = ERROR_SUCCESS;
            std::wstring owner;
            std::wstring target;
            if (!utf8ToUtf16(ownerKey, owner, nativeCode) || !utf8ToUtf16(normalizedTarget, target, nativeCode))
            {
                return {};
            }
            result += owner;
            result.push_back(L'.');
            result += target;
            for (wchar_t &character : result)
            {
                if ((character < L'A' || character > L'Z') && (character < L'a' || character > L'z') && (character < L'0' || character > L'9') &&
                    character != L'.' && character != L'_' && character != L'-')
                {
                    character = L'_';
                }
            }
            return result;
        }

        [[nodiscard]] IO::Types::Status readOwner(HKEY targetKey, std::string &owner, bool &hasOwner)
        {
            std::wstring ownerUtf16;
            IO::Types::Status status = readRegistryString(targetKey, L"GameWIP.OwnerKey", ownerUtf16);
            if (!status.ok())
            {
                hasOwner = false;
                if (status.code == ErrorCode::NotFound)
                {
                    return IO::successStatus();
                }
                return status;
            }
            DWORD nativeCode = ERROR_SUCCESS;
            if (!utf16ToUtf8(ownerUtf16, owner, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            hasOwner = true;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status writeLaunchCommand(HKEY commandKey, const Types::Shell::LaunchActionView &action)
        {
            std::wstring executable;
            std::wstring arguments;
            IO::Types::Status status = makeLaunchCommand(action, true, executable, arguments);
            if (!status.ok())
            {
                return status;
            }
            std::wstring command = L"\"";
            command += executable;
            command += L"\"";
            if (!arguments.empty())
            {
                command.push_back(L' ');
                command += arguments;
            }
            return writeRegistryString(commandKey, L"", command);
        }

        [[nodiscard]] IO::Types::Status writeRegistrationProgram(
            std::wstring_view progId,
            std::string_view ownerKey,
            std::string_view displayName,
            std::string_view contentType,
            const std::optional<Types::Registration::Icon> &icon,
            const std::optional<Types::Shell::LaunchActionView> &defaultAction,
            std::span<const Types::Registration::Verb> verbs)
        {
            RegistryKey program;
            IO::Types::Status status =
                openRegistryKey(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\") + std::wstring(progId), KEY_READ | KEY_WRITE, program, true);
            if (!status.ok())
            {
                return status;
            }
            DWORD nativeCode = ERROR_SUCCESS;
            std::wstring owner;
            std::wstring display;
            std::wstring type;
            if (!utf8ToUtf16(ownerKey, owner, nativeCode) || !utf8ToUtf16(displayName, display, nativeCode) ||
                (!contentType.empty() && !utf8ToUtf16(contentType, type, nativeCode)))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            status = writeRegistryString(program.get(), L"GameWIP.OwnerKey", owner);
            if (!status.ok())
            {
                return status;
            }
            status = writeRegistryString(program.get(), L"", display);
            if (!status.ok())
            {
                return status;
            }
            if (!type.empty())
            {
                status = writeRegistryString(program.get(), L"Content Type", type);
                if (!status.ok())
                {
                    return status;
                }
            }
            else
            {
                static_cast<void>(RegDeleteValueW(program.get(), L"Content Type"));
            }

            static_cast<void>(RegDeleteTreeW(program.get(), L"DefaultIcon"));
            if (icon.has_value())
            {
                RegistryKey iconKey;
                status = openRegistryKey(program.get(), L"DefaultIcon", KEY_READ | KEY_WRITE, iconKey, true);
                if (!status.ok())
                {
                    return status;
                }
                std::wstring path = icon->path.path.get().wstring();
                std::wstring iconValue = L"\"" + path + L"\"," + std::to_wstring(icon->index);
                status = writeRegistryString(iconKey.get(), L"", iconValue);
                if (!status.ok())
                {
                    return status;
                }
            }

            static_cast<void>(RegDeleteTreeW(program.get(), L"shell"));
            if (defaultAction.has_value() || !verbs.empty())
            {
                RegistryKey shell;
                status = openRegistryKey(program.get(), L"shell", KEY_READ | KEY_WRITE, shell, true);
                if (!status.ok())
                {
                    return status;
                }
                if (defaultAction.has_value())
                {
                    RegistryKey open;
                    status = openRegistryKey(shell.get(), L"open", KEY_READ | KEY_WRITE, open, true);
                    if (!status.ok())
                    {
                        return status;
                    }
                    std::wstring label = L"Open";
                    status = writeRegistryString(open.get(), L"", label);
                    if (!status.ok())
                    {
                        return status;
                    }
                    RegistryKey command;
                    status = openRegistryKey(open.get(), L"command", KEY_READ | KEY_WRITE, command, true);
                    if (!status.ok())
                    {
                        return status;
                    }
                    status = writeLaunchCommand(command.get(), *defaultAction);
                    if (!status.ok())
                    {
                        return status;
                    }
                }
                for (const Types::Registration::Verb &verb : verbs)
                {
                    DWORD verbCode = ERROR_SUCCESS;
                    std::wstring verbName;
                    std::wstring verbLabel;
                    if (!utf8ToUtf16(verb.name, verbName, verbCode) || !utf8ToUtf16(verb.label, verbLabel, verbCode) ||
                        verbName.find_first_of(L"\\/") != std::wstring::npos)
                    {
                        return IO::makeStatus(ErrorCode::InvalidArgument);
                    }
                    RegistryKey verbKey;
                    status = openRegistryKey(shell.get(), verbName, KEY_READ | KEY_WRITE, verbKey, true);
                    if (!status.ok())
                    {
                        return status;
                    }
                    status = writeRegistryString(verbKey.get(), L"", verbLabel);
                    if (!status.ok())
                    {
                        return status;
                    }
                    RegistryKey command;
                    status = openRegistryKey(verbKey.get(), L"command", KEY_READ | KEY_WRITE, command, true);
                    if (!status.ok())
                    {
                        return status;
                    }
                    status = writeLaunchCommand(command.get(), verb.action);
                    if (!status.ok())
                    {
                        return status;
                    }
                }
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status inspectTarget(
            std::wstring_view targetPath,
            std::string_view ownerKey,
            bool &exists,
            bool &sameOwner,
            std::string &existingOwner)
        {
            RegistryKey key;
            IO::Types::Status status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ, key, false);
            if (!status.ok())
            {
                if (status.code == ErrorCode::NotFound)
                {
                    exists = false;
                    sameOwner = false;
                    existingOwner.clear();
                    return IO::successStatus();
                }
                return status;
            }
            exists = true;
            bool hasOwner = false;
            status = readOwner(key.get(), existingOwner, hasOwner);
            if (!status.ok())
            {
                return status;
            }
            std::wstring defaultValue;
            const IO::Types::Status defaultStatus = readRegistryString(key.get(), L"", defaultValue);
            if (!defaultStatus.ok() && defaultStatus.code != ErrorCode::NotFound)
            {
                return defaultStatus;
            }
            if (!hasOwner && defaultValue.empty())
            {
                exists = false;
            }
            sameOwner = hasOwner && existingOwner == ownerKey;
            return IO::successStatus();
        }

        [[nodiscard]] Types::Registration::Conflict makeConflict(Types::Registration::TargetKind kind, std::string target, std::string owner) noexcept
        {
            return {.kind = kind, .target = std::move(target), .ownerKey = std::move(owner)};
        }

        [[nodiscard]] Types::Registration::Result unregisterRegistrationTarget(
            std::string_view ownerKey,
            std::string_view input,
            bool extension) noexcept
        {
            Types::Registration::Result result{IO::successStatus(), {}, false};
            try
            {
                std::string target;
                std::wstring targetWide;
                IO::Types::Status status = normalizeTarget(input, extension, target, targetWide);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                const std::wstring targetPath = L"Software\\Classes\\" + targetWide;
                bool exists = false;
                bool sameOwner = false;
                std::string existingOwner;
                status = inspectTarget(targetPath, ownerKey, exists, sameOwner, existingOwner);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                const Types::Registration::TargetKind kind =
                    extension ? Types::Registration::TargetKind::FileExtension : Types::Registration::TargetKind::UriScheme;
                if (exists && !sameOwner)
                {
                    result.conflicts.push_back(makeConflict(kind, target, existingOwner));
                }

                const std::wstring progId = registrationProgId(extension, ownerKey, target);
                const std::wstring progPath = L"Software\\Classes\\" + progId;
                bool ownProgram = false;
                RegistryKey program;
                status = openRegistryKey(HKEY_CURRENT_USER, progPath, KEY_READ, program, false);
                if (status.ok())
                {
                    std::string programOwner;
                    bool hasOwner = false;
                    status = readOwner(program.get(), programOwner, hasOwner);
                    if (!status.ok())
                    {
                        result.status = status;
                        return result;
                    }
                    ownProgram = hasOwner && programOwner == ownerKey;
                }
                else if (status.code != ErrorCode::NotFound)
                {
                    result.status = status;
                    return result;
                }

                if (sameOwner)
                {
                    const LONG deleteResult = RegDeleteTreeW(HKEY_CURRENT_USER, targetPath.c_str());
                    if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND)
                    {
                        result.status = registryStatus(deleteResult, "delete shell registration target");
                        return result;
                    }
                }
                if (ownProgram)
                {
                    const LONG deleteResult = RegDeleteTreeW(HKEY_CURRENT_USER, progPath.c_str());
                    if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND)
                    {
                        result.status = registryStatus(deleteResult, "delete shell registration program");
                        return result;
                    }
                }
                if (!sameOwner && !ownProgram)
                {
                    result.status = exists ? IO::makeStatus(ErrorCode::AlreadyExists) : IO::makeStatus(ErrorCode::NotFound);
                    return result;
                }
                result.replacedOwned = true;
                return result;
            }
            catch (const std::bad_alloc &)
            {
                result.status = IO::makeStatus(ErrorCode::OutOfMemory);
                return result;
            }
            catch (...)
            {
                result.status = IO::makeStatus(ErrorCode::Unknown);
                return result;
            }
        }
    } // namespace

    // ------------------------------------------------------------
    // Capability and taskbar integration
    // ------------------------------------------------------------

    Types::Shell::CapabilitiesResult getShellCapabilities() noexcept
    {
        static const Types::Shell::Capabilities capabilities = []
        {
            Types::Shell::Capabilities value;
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::Taskbar);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TaskbarProgress);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TaskbarOverlayIcon);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TaskbarThumbnailButtons);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayIcon);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenu);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenuIcons);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenuAccelerators);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenuBadges);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::Notifications);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::NotificationSound);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::JumpLists);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::RecentItems);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::FileAssociationRegistration);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::UriSchemeRegistration);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::RegistrationVerbs);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::RegistrationExtendedMetadata);
            return value;
        }();
        return {.status = IO::successStatus(), .capabilities = capabilities};
    }

    IO::Types::Status openTaskbar(TaskbarItemState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeOpen))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        if (state.window == nullptr)
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        const NativeHandleView nativeHandleView = nativeHandle(*state.window);
        if (nativeHandleView.window == nullptr)
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        try
        {
            auto *native = new TaskbarNative();
            native->window = static_cast<HWND>(nativeHandleView.window);
            state.platform = native;
            IO::Types::Status status = applyTaskbar(state);
            if (!status.ok())
            {
                closeTaskbarBestEffort(state);
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status applyTaskbar(TaskbarItemState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeApply))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<TaskbarNative *>(state.platform);
        if (native == nullptr || native->window == nullptr)
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        std::vector<HICON> newIcons;
        const auto cleanupIcons = [&]() noexcept
        {
            for (HICON icon : newIcons)
            {
                if (icon != nullptr)
                {
                    DestroyIcon(icon);
                }
            }
            newIcons.clear();
        };
        try
        {
            IO::Types::Status status = applyTaskbarButtons(*native, state.thumbnailButtons, newIcons);
            if (!status.ok())
            {
                return status;
            }
            status = applyTaskbarProgress(*native, state.progress);
            if (!status.ok())
            {
                cleanupIcons();
                return status;
            }

            ApartmentLease apartment;
            if (!apartment.status().ok())
            {
                cleanupIcons();
                return apartment.status();
            }
            ComPtr<ITaskbarList3> taskbar;
            HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()));
            if (FAILED(result))
            {
                cleanupIcons();
                return statusFromHResult(result);
            }
            result = taskbar->HrInit();
            if (FAILED(result))
            {
                cleanupIcons();
                return statusFromHResult(result);
            }
            HICON newOverlay = nullptr;
            if (state.overlayIcon.has_value())
            {
                newOverlay = createIcon(*state.overlayIcon);
                if (newOverlay == nullptr)
                {
                    cleanupIcons();
                    return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "create taskbar overlay icon");
                }
            }
            result = taskbar->SetOverlayIcon(native->window, newOverlay, L"");
            if (FAILED(result))
            {
                if (newOverlay != nullptr)
                {
                    DestroyIcon(newOverlay);
                }
                cleanupIcons();
                return statusFromHResult(result);
            }
            if (native->overlay != nullptr)
            {
                DestroyIcon(native->overlay);
            }
            native->overlay = newOverlay;
            for (HICON icon : native->buttonIcons)
            {
                if (icon != nullptr)
                {
                    DestroyIcon(icon);
                }
            }
            native->buttonIcons = std::move(newIcons);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            cleanupIcons();
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            cleanupIcons();
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status closeTaskbar(TaskbarItemState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeClose))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<TaskbarNative *>(state.platform);
        if (native == nullptr)
        {
            state.open = false;
            return IO::successStatus();
        }
        if (native->overlay != nullptr)
        {
            ApartmentLease apartment;
            if (apartment.status().ok())
            {
                ComPtr<ITaskbarList3> taskbar;
                if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar.put()))) &&
                    SUCCEEDED(taskbar->HrInit()))
                {
                    static_cast<void>(taskbar->SetOverlayIcon(native->window, nullptr, nullptr));
                }
            }
            DestroyIcon(native->overlay);
        }
        for (HICON icon : native->buttonIcons)
        {
            if (icon != nullptr)
            {
                DestroyIcon(icon);
            }
        }
        delete native;
        state.platform = nullptr;
        state.open = false;
        return IO::successStatus();
    }

    void closeTaskbarBestEffort(TaskbarItemState &state) noexcept
    {
        static_cast<void>(closeTaskbar(state));
    }

    void notifyTaskbarWindowDestroyed(WindowState &window) noexcept
    {
        if (window.taskbarItem != nullptr)
        {
            TaskbarItemState *taskbar = window.taskbarItem;
            auto *native = static_cast<TaskbarNative *>(taskbar->platform);
            if (native != nullptr)
            {
                if (native->overlay != nullptr)
                {
                    DestroyIcon(native->overlay);
                }
                for (HICON icon : native->buttonIcons)
                {
                    if (icon != nullptr)
                    {
                        DestroyIcon(icon);
                    }
                }
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
        if (taskbar == nullptr || taskbar->eventQueue == nullptr || !taskbar->open)
        {
            return;
        }
        for (const OwnedThumbnailButton &button : taskbar->thumbnailButtons)
        {
            if (button.id.value == commandId && button.enabled)
            {
                publishShellEvent(*taskbar->eventQueue, Types::Shell::Events::TaskbarThumbnailButtonInvoked{window.id, button.id});
                return;
            }
        }
    }

    // ------------------------------------------------------------
    // Tray integration
    // ------------------------------------------------------------

    IO::Types::Status openTray(TrayIconState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeOpen))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        IO::Types::Status status = ensureTrayWindowClass();
        if (!status.ok())
        {
            return status;
        }
        try
        {
            auto *native = new TrayNative();
            native->state = &state;
            native->taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
            if (native->taskbarCreatedMessage == 0)
            {
                delete native;
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "RegisterWindowMessageW TaskbarCreated");
            }
            state.platform = native;
            native->window =
                CreateWindowExW(0, kTrayWindowClassName, L"GameWIP tray", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), native);
            if (native->window == nullptr)
            {
                state.platform = nullptr;
                delete native;
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "CreateWindowExW tray message window");
            }
            state.open = true;
            status = applyTray(state);
            if (!status.ok())
            {
                closeTrayBestEffort(state);
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status applyTray(TrayIconState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeApply))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<TrayNative *>(state.platform);
        if (native == nullptr || native->window == nullptr || state.icons.empty())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        const OwnedIconImage &selectedIcon = closestIcon(state.icons, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
        HICON newIcon = createIcon(selectedIcon);
        if (newIcon == nullptr)
        {
            return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "create tray icon");
        }
        try
        {
            std::wstring tooltip;
            DWORD nativeCode = ERROR_SUCCESS;
            if (!utf8ToUtf16(state.tooltip, tooltip, nativeCode))
            {
                DestroyIcon(newIcon);
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = native->window;
            data.uID = static_cast<UINT>(state.id.value);
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
            data.uCallbackMessage = kTrayCallbackMessage;
            data.hIcon = newIcon;
            const std::size_t length = std::min<std::size_t>(tooltip.size(), ARRAYSIZE(data.szTip) - 1);
            std::copy_n(tooltip.data(), length, data.szTip);
            const DWORD operation = native->icon == nullptr ? NIM_ADD : NIM_MODIFY;
            if (Shell_NotifyIconW(operation, &data) == FALSE)
            {
                const DWORD errorCode = GetLastError();
                DestroyIcon(newIcon);
                return statusFromWin32(ErrorCode::NativeFailure, errorCode, "Shell_NotifyIconW tray icon");
            }
            if (native->icon == nullptr)
            {
                data.uVersion = NOTIFYICON_VERSION_4;
                static_cast<void>(Shell_NotifyIconW(NIM_SETVERSION, &data));
            }
            if (native->icon != nullptr)
            {
                DestroyIcon(native->icon);
            }
            native->icon = newIcon;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            DestroyIcon(newIcon);
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            DestroyIcon(newIcon);
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status closeTray(TrayIconState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeClose))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<TrayNative *>(state.platform);
        if (native == nullptr)
        {
            state.open = false;
            return IO::successStatus();
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = native->window;
        data.uID = static_cast<UINT>(state.id.value);
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        if (native->window != nullptr)
        {
            DestroyWindow(native->window);
        }
        if (native->icon != nullptr)
        {
            DestroyIcon(native->icon);
        }
        delete native;
        state.platform = nullptr;
        state.open = false;
        return IO::successStatus();
    }

    void closeTrayBestEffort(TrayIconState &state) noexcept
    {
        static_cast<void>(closeTray(state));
    }

    // ------------------------------------------------------------
    // Notification integration
    // ------------------------------------------------------------

    IO::Types::Status openNotificationCenter(NotificationCenterState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeOpen))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        IO::Types::Status status = ensureNotificationWindowClass();
        if (!status.ok())
        {
            return status;
        }
        try
        {
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
            if (native->window == nullptr)
            {
                state.platform = nullptr;
                delete native;
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "CreateWindowExW notification message window");
            }
            state.open = true;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status publishNotification(NotificationCenterState &state, std::uint64_t id, const OwnedNotificationDescription &description) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeApply))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<NotificationNative *>(state.platform);
        if (native == nullptr || native->window == nullptr)
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        try
        {
            return showBasicNotification(*native, id, description, true);
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status updateNotification(NotificationCenterState &state, std::uint64_t id, const OwnedNotificationDescription &description) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeApply))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<NotificationNative *>(state.platform);
        if (native == nullptr || native->window == nullptr)
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        try
        {
            return showBasicNotification(*native, id, description, false);
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status dismissNotification(NotificationCenterState &state, std::uint64_t id) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeApply))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<NotificationNative *>(state.platform);
        if (native == nullptr || native->window == nullptr)
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = native->window;
        data.uID = static_cast<UINT>(id);
        if (Shell_NotifyIconW(NIM_DELETE, &data) == FALSE)
        {
            const DWORD nativeCode = GetLastError();
            if (nativeCode != ERROR_FILE_NOT_FOUND)
            {
                return statusFromWin32(ErrorCode::NativeFailure, nativeCode, "Shell_NotifyIconW dismiss notification");
            }
        }
        return IO::successStatus();
    }

    IO::Types::Status closeNotificationCenter(NotificationCenterState &state) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::ShellNativeClose))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }
        auto *native = static_cast<NotificationNative *>(state.platform);
        if (native == nullptr)
        {
            state.open = false;
            return IO::successStatus();
        }
        for (const auto &entry : state.notifications)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = native->window;
            data.uID = static_cast<UINT>(entry.first);
            static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        }
        if (native->window != nullptr)
        {
            DestroyWindow(native->window);
        }
        delete native;
        state.platform = nullptr;
        state.open = false;
        return IO::successStatus();
    }

    void closeNotificationCenterBestEffort(NotificationCenterState &state) noexcept
    {
        static_cast<void>(closeNotificationCenter(state));
    }

    // ------------------------------------------------------------
    // Jump lists and shell registration
    // ------------------------------------------------------------

    IO::Types::Status publishJumpLists(const Types::JumpLists::Description &description) noexcept
    {
        ApartmentLease apartment;
        if (!apartment.status().ok())
        {
            return apartment.status();
        }
        try
        {
            std::vector<std::wstring> recentItems;
            recentItems.reserve(description.recentItems.size());
            for (const Types::Shell::TargetView &target : description.recentItems)
            {
                std::wstring recent;
                DWORD nativeCode = ERROR_SUCCESS;
                if (const auto *path = std::get_if<Types::Shell::PathTargetView>(&target))
                {
                    recent = path->path.get().wstring();
                }
                else if (const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target))
                {
                    if (!utf8ToUtf16(uri->uri, recent, nativeCode))
                    {
                        return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    }
                }
                else
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                recentItems.push_back(std::move(recent));
            }
            ComPtr<ICustomDestinationList> list;
            HRESULT result = CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(list.put()));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            UINT maximumSlots = 0;
            ComPtr<IObjectArray> removed;
            result = list->BeginList(&maximumSlots, IID_PPV_ARGS(removed.put()));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            JumpListTransaction transaction(*list.get());

            if (!description.tasks.empty())
            {
                ComPtr<IObjectCollection> collection;
                result = CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(collection.put()));
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
                for (const Types::JumpLists::Task &task : description.tasks)
                {
                    ComPtr<IShellLinkW> link;
                    IO::Types::Status status = createJumpLink(task, link);
                    if (!status.ok())
                    {
                        return status;
                    }
                    result = collection->AddObject(link.get());
                    if (FAILED(result))
                    {
                        return statusFromHResult(result);
                    }
                }
                ComPtr<IObjectArray> objects;
                result = collection->QueryInterface(IID_PPV_ARGS(objects.put()));
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
                result = list->AddUserTasks(objects.get());
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }
            for (const Types::JumpLists::Category &category : description.categories)
            {
                std::wstring label;
                DWORD nativeCode = ERROR_SUCCESS;
                if (!utf8ToUtf16(category.label, label, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }
                IO::Types::Status status = appendJumpTasks(list.get(), label, category.tasks);
                if (!status.ok())
                {
                    return status;
                }
            }
            result = list->CommitList();
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            transaction.commit();

            for (const std::wstring &recent : recentItems)
            {
                SHAddToRecentDocs(SHARD_PATHW, recent.c_str());
            }
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    Types::Registration::Result registerFileExtension(const Types::Registration::FileExtensionDescription &description) noexcept
    {
        Types::Registration::Result result{IO::successStatus(), {}, false};
        try
        {
            std::string target;
            std::wstring targetWide;
            IO::Types::Status status = normalizeTarget(description.extension, true, target, targetWide);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            const std::wstring targetPath = L"Software\\Classes\\" + targetWide;
            bool exists = false;
            bool sameOwner = false;
            std::string existingOwner;
            status = inspectTarget(targetPath, description.ownerKey, exists, sameOwner, existingOwner);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            if (exists && !sameOwner)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::FileExtension, target, existingOwner));
                if (description.conflictPolicy == Types::Registration::ConflictPolicy::Report ||
                    description.conflictPolicy == Types::Registration::ConflictPolicy::ReplaceOwned)
                {
                    result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                    return result;
                }
            }
            else if (exists && description.conflictPolicy == Types::Registration::ConflictPolicy::Report)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::FileExtension, target, std::string(description.ownerKey)));
                result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                return result;
            }

            const std::wstring progId = registrationProgId(true, description.ownerKey, target);
            if (progId.empty())
            {
                result.status = IO::makeStatus(ErrorCode::EncodingFailed);
                return result;
            }
            const std::wstring progPath = L"Software\\Classes\\" + progId;
            RegistryTreeBackup transaction(targetPath, progPath);
            status = transaction.capture();
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            status = writeRegistrationProgram(
                progId,
                description.ownerKey,
                description.displayName,
                description.contentType,
                description.icon,
                description.defaultAction,
                description.verbs);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            if (!exists || sameOwner)
            {
                RegistryKey targetKey;
                status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ | KEY_WRITE, targetKey, true);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                DWORD nativeCode = ERROR_SUCCESS;
                std::wstring owner;
                if (!utf8ToUtf16(description.ownerKey, owner, nativeCode))
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    return result;
                }
                status = writeRegistryString(targetKey.get(), L"GameWIP.OwnerKey", owner);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                status = writeRegistryString(targetKey.get(), L"", progId);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
            }
            result.replacedOwned = sameOwner;
            transaction.commit();
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Registration::Result registerUriScheme(const Types::Registration::UriSchemeDescription &description) noexcept
    {
        Types::Registration::Result result{IO::successStatus(), {}, false};
        try
        {
            std::string target;
            std::wstring targetWide;
            IO::Types::Status status = normalizeTarget(description.scheme, false, target, targetWide);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            const std::wstring targetPath = L"Software\\Classes\\" + targetWide;
            bool exists = false;
            bool sameOwner = false;
            std::string existingOwner;
            status = inspectTarget(targetPath, description.ownerKey, exists, sameOwner, existingOwner);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            if (exists && !sameOwner)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::UriScheme, target, existingOwner));
                if (description.conflictPolicy == Types::Registration::ConflictPolicy::Report ||
                    description.conflictPolicy == Types::Registration::ConflictPolicy::ReplaceOwned)
                {
                    result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                    return result;
                }
            }
            else if (exists && description.conflictPolicy == Types::Registration::ConflictPolicy::Report)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::UriScheme, target, std::string(description.ownerKey)));
                result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                return result;
            }

            const std::wstring progId = registrationProgId(false, description.ownerKey, target);
            if (progId.empty())
            {
                result.status = IO::makeStatus(ErrorCode::EncodingFailed);
                return result;
            }
            const std::wstring progPath = L"Software\\Classes\\" + progId;
            RegistryTreeBackup transaction(targetPath, progPath);
            status = transaction.capture();
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            status = writeRegistrationProgram(
                progId,
                description.ownerKey,
                description.displayName,
                {},
                description.icon,
                description.defaultAction,
                description.verbs);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            if (!exists || sameOwner)
            {
                RegistryKey targetKey;
                status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ | KEY_WRITE, targetKey, true);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                DWORD nativeCode = ERROR_SUCCESS;
                std::wstring owner;
                if (!utf8ToUtf16(description.ownerKey, owner, nativeCode))
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    return result;
                }
                status = writeRegistryString(targetKey.get(), L"GameWIP.OwnerKey", owner);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                status = writeRegistryString(targetKey.get(), L"", progId);
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
                status = writeRegistryString(targetKey.get(), L"URL Protocol", L"");
                if (!status.ok())
                {
                    result.status = status;
                    return result;
                }
            }
            result.replacedOwned = sameOwner;
            transaction.commit();
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Registration::Result unregisterFileExtension(std::string_view ownerKey, std::string_view extension) noexcept
    {
        return unregisterRegistrationTarget(ownerKey, extension, true);
    }

    Types::Registration::Result unregisterUriScheme(std::string_view ownerKey, std::string_view scheme) noexcept
    {
        return unregisterRegistrationTarget(ownerKey, scheme, false);
    }
} // namespace GameWIP::Desktop::Detail::Platform
