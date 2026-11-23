/// @file win32_tray.cpp
/// @brief Implements Win32 tray integration.

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/shell.h"
#include "desktop/platform/win32/internal/win32_pixel_helpers.h"
#include "desktop/platform/win32/internal/win32_shell_helpers.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Tray helpers
    /// ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;
        using Win32::closestIcon;
        using Win32::copyRgbaToBgra;
        using Win32::createIcon;
        using Win32::kTrayCallbackMessage;

        /// @brief Owns the hidden message HWND and icon used by one tray resource.
        struct TrayNative
        {
            // Portable owner and message window
            TrayIconState *state = nullptr;
            HWND window = nullptr;

            // Shell icon and callback negotiation
            HICON icon = nullptr;
            UINT taskbarCreatedMessage = 0;
            bool version4 = false;
        };

        /// @brief Destroys a submenu until ownership passes to its parent menu.
        class MenuOwner final
        {
        public:
            MenuOwner() noexcept = default;
            MenuOwner(const MenuOwner &) = delete;
            MenuOwner &operator=(const MenuOwner &) = delete;

            ~MenuOwner() noexcept
            {
                reset();
            }

            [[nodiscard]] HMENU get() const noexcept
            {
                return value_;
            }

            void reset(HMENU value = nullptr) noexcept
            {
                if(value_ != nullptr)
                {
                    DestroyMenu(value_);
                }
                value_ = value;
            }

            [[nodiscard]] HMENU release() noexcept
            {
                HMENU value = value_;
                value_ = nullptr;
                return value;
            }

        private:
            HMENU value_ = nullptr;
        };

        inline constexpr wchar_t kTrayWindowClassName[] = L"GameWIP.Shell.TrayMessage";
        // Each icon owns its HWND. Version 4 packs the native ID into only 16 bits.
        inline constexpr UINT kTrayNativeId = 1;

        /// @brief Converts retained RGBA8 pixels into a menu-compatible DIB bitmap.
        [[nodiscard]] HBITMAP createMenuBitmap(const OwnedIconImage &image) noexcept
        {
            if(image.size.width == 0 || image.size.height == 0 || image.rgba8.empty())
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

            if(screen == nullptr)
            {
                return nullptr;
            }

            void *pixels = nullptr;
            HBITMAP bitmap = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO *>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);

            if(bitmap == nullptr || pixels == nullptr)
            {
                if(bitmap != nullptr)
                {
                    DeleteObject(bitmap);
                }

                return nullptr;
            }
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            std::span<std::byte> destination(static_cast<std::byte *>(pixels), image.rgba8.size());
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

            if(!copyRgbaToBgra(image.rgba8, destination))
            {
                DeleteObject(bitmap);
                return nullptr;
            }
            return bitmap;
        }

        /// @brief Builds one tray menu label with accelerator and badge suffixes.
        [[nodiscard]] bool menuText(const OwnedTrayMenuItem &item, std::wstring &text)
        {
            std::string utf8 = item.label;

            if(!item.accelerator.empty())
            {
                utf8 += '\t';
                utf8 += item.accelerator;
            }

            if(!item.badge.empty())
            {
                utf8 += " [";
                utf8 += item.badge;
                utf8 += ']';
            }
            DWORD nativeCode = ERROR_SUCCESS;
            return utf8ToUtf16(utf8, text, nativeCode);
        }

        /// @brief Recursively creates native tray menu items and records bitmap ownership.
        [[nodiscard]] bool appendMenuItems(HMENU menu, const std::vector<OwnedTrayMenuItem> &items, std::vector<HBITMAP> &bitmaps)
        {
            for(const OwnedTrayMenuItem &item : items)
            {
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);

                // Insert separators without creating text or command state.
                if(item.kind == Types::Tray::MenuItemKind::Separator)
                {
                    info.fMask = MIIM_FTYPE;
                    info.fType = MFT_SEPARATOR;

                    if(InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &info) == FALSE)
                    {
                        return false;
                    }
                    continue;
                }

                // Prepare the label and shared state flags.
                std::wstring text;

                if(!menuText(item, text))
                {
                    return false;
                }
                info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
                info.fType = MFT_STRING;
                info.fState = item.enabled ? MFS_ENABLED : MFS_DISABLED;

                if((item.kind == Types::Tray::MenuItemKind::Check || item.kind == Types::Tray::MenuItemKind::Radio) && item.checked)
                {
                    info.fState |= MFS_CHECKED;
                }

                if(item.kind == Types::Tray::MenuItemKind::Radio)
                {
                    info.fType |= MFT_RADIOCHECK;
                }
                info.dwTypeData = text.data();
                MenuOwner submenu;

                // Recursively build submenus; leaf items retain their command ID.
                if(item.kind == Types::Tray::MenuItemKind::Submenu)
                {
                    submenu.reset(CreatePopupMenu());

                    if(submenu.get() == nullptr)
                    {
                        return false;
                    }

                    if(!appendMenuItems(submenu.get(), item.children, bitmaps))
                    {
                        return false;
                    }

                    info.fMask |= MIIM_SUBMENU;
                    info.hSubMenu = submenu.get();
                }
                else
                {
                    info.fMask |= MIIM_ID;
                    info.wID = item.commandId.value;
                }

                // Retain optional bitmaps before inserting the native item.
                if(item.icon.has_value())
                {
                    HBITMAP bitmap = createMenuBitmap(*item.icon);

                    if(bitmap == nullptr)
                    {
                        return false;
                    }

                    try
                    {
                        bitmaps.push_back(bitmap);
                    }
                    catch(...)
                    {
                        DeleteObject(bitmap);
                        throw;
                    }

                    info.fMask |= MIIM_BITMAP;
                    info.hbmpItem = bitmap;
                }

                if(InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &info) == FALSE)
                {
                    return false;
                }
                static_cast<void>(submenu.release());
            }

            return true;
        }

        /// @brief Releases temporary menu bitmaps after the popup menu is dismissed.
        void destroyMenuBitmaps(std::vector<HBITMAP> &bitmaps) noexcept
        {
            for(HBITMAP bitmap : bitmaps)
            {
                if(bitmap != nullptr)
                {
                    DeleteObject(bitmap);
                }
            }
            bitmaps.clear();
        }

        /// @brief Publishes a tray activation with the best-effort cursor screen position.
        void publishTrayActivation(
            TrayNative &native,
            Types::Shell::Events::TrayActivationKind kind,
            std::optional<Types::ScreenPosition> position = std::nullopt) noexcept
        {
            if(native.state == nullptr || native.state->eventQueue == nullptr)
            {
                return;
            }

            POINT point{};

            if(!position && GetCursorPos(&point) != FALSE)
            {
                position = Types::ScreenPosition{point.x, point.y};
            }
            publishShellEvent(*native.state->eventQueue, Types::Shell::Events::TrayActivated{native.state->id, kind, position});
        }

        /// @brief Builds, shows, and tears down the owner-thread tray popup menu.
        void showTrayMenu(TrayNative &native, std::optional<Types::ScreenPosition> position = std::nullopt) noexcept
        {
            if(native.state == nullptr || native.state->eventQueue == nullptr || native.state->menu.empty())
            {
                return;
            }
            HMENU menu = nullptr;
            std::vector<HBITMAP> bitmaps;
            try
            {
                // Build every native menu item before displaying the popup.
                menu = CreatePopupMenu();

                if(menu == nullptr)
                {
                    return;
                }

                if(!appendMenuItems(menu, native.state->menu, bitmaps))
                {
                    destroyMenuBitmaps(bitmaps);
                    DestroyMenu(menu);
                    return;
                }

                // Resolve the screen position and show the owner-thread menu.
                POINT point{};

                if(position)
                {
                    point = {position->x, position->y};
                }
                else if(GetCursorPos(&point) == FALSE)
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

                // Forward a selected command after releasing menu resources.
                if(command != 0)
                {
                    publishShellEvent(
                        *native.state->eventQueue,
                        Types::Shell::Events::TrayCommandInvoked{native.state->id, Types::Shell::CommandId{command}});
                }
            }
            catch(...)
            {
                destroyMenuBitmaps(bitmaps);

                if(menu != nullptr)
                {
                    DestroyMenu(menu);
                }
            }
        }

        /// @brief Routes tray recreation, activation, and command messages.
        LRESULT CALLBACK trayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *native = reinterpret_cast<TrayNative *>(GetWindowLongPtrW(window, GWLP_USERDATA));

            if(message == WM_NCCREATE)
            {
                // Bind the native message window to its portable tray state.
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
                native = static_cast<TrayNative *>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(native));

                if(native != nullptr)
                {
                    native->window = window;
                    retainShellMessageWindow();
                }
            }

            if(native == nullptr)
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }

            if(message == WM_NCDESTROY)
            {
                // Clear native ownership before forwarding final destruction.
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                native->window = nullptr;
                releaseShellMessageWindow();
                return DefWindowProcW(window, message, wParam, lParam);
            }

            if(message == native->taskbarCreatedMessage)
            {
                // Re-add the icon after Explorer restarts.
                try
                {
                    NOTIFYICONDATAW data{};
                    data.cbSize = sizeof(data);
                    data.hWnd = window;
                    data.uID = kTrayNativeId;
                    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
                    data.uCallbackMessage = kTrayCallbackMessage;
                    data.hIcon = native->icon;
                    DWORD nativeCode = ERROR_SUCCESS;
                    std::wstring tooltip;

                    if(utf8ToUtf16(native->state->tooltip, tooltip, nativeCode))
                    {
                        const std::size_t length = std::min<std::size_t>(tooltip.size(), ARRAYSIZE(data.szTip) - 1);
                        std::copy_n(tooltip.data(), length, data.szTip);
                    }
                    native->version4 = false;

                    if(Shell_NotifyIconW(NIM_ADD, &data) != FALSE)
                    {
                        data.uVersion = NOTIFYICON_VERSION_4;
                        native->version4 = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
                    }
                }
                catch(...)
                {
                    SetLastError(ERROR_GEN_FAILURE);
                }

                return 0;
            }

            if(message == kTrayCallbackMessage)
            {
                // Normalize versioned shell callbacks into portable activations.
                const UINT event = native->version4 ? LOWORD(lParam) : static_cast<UINT>(lParam);
                const std::optional<Types::ScreenPosition> position =
                    native->version4 ? std::optional{Types::ScreenPosition{static_cast<short>(LOWORD(wParam)), static_cast<short>(HIWORD(wParam))}}
                                     : std::nullopt;

                switch(event)
                {
                case WM_LBUTTONUP:
                    if(native->version4)
                    {
                        return 0; // NIN_SELECT supplies the version-4 primary activation.
                    }
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::Primary);
                    return 0;
                case WM_LBUTTONDBLCLK:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::DoublePrimary, position);
                    return 0;
                case WM_RBUTTONUP:
                    if(native->version4)
                    {
                        return 0; // WM_CONTEXTMENU covers both mouse and keyboard menus.
                    }
                    [[fallthrough]];
                case WM_CONTEXTMENU:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::Secondary, position);
                    showTrayMenu(*native, position);
                    return 0;
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    publishTrayActivation(*native, Types::Shell::Events::TrayActivationKind::Primary, position);
                    return 0;
                default:
                    break;
                }
            }

            return DefWindowProcW(window, message, wParam, lParam);
        }

        [[nodiscard]] IO::Types::Status ensureTrayWindowClass() noexcept
        {
            WNDCLASSEXW existing{};
            existing.cbSize = sizeof(existing);
            HINSTANCE instance = GetModuleHandleW(nullptr);

            if(GetClassInfoExW(instance, kTrayWindowClassName, &existing) != FALSE)
            {
                return IO::successStatus();
            }

            WNDCLASSEXW classInfo{};
            classInfo.cbSize = sizeof(classInfo);
            classInfo.lpfnWndProc = trayWindowProc;
            classInfo.hInstance = instance;
            classInfo.lpszClassName = kTrayWindowClassName;

            if(RegisterClassExW(&classInfo) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "RegisterClassExW tray window");
            }

            return IO::successStatus();
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Tray integration
    /// ------------------------------------------------------------

    IO::Types::Status openTray(TrayIconState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeOpen))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }

        IO::Types::Status status = ensureTrayWindowClass();

        if(!status.ok())
        {
            return status;
        }

        try
        {
            // Create the hidden owner window used for shell callbacks.
            auto *native = new TrayNative();
            native->state = &state;
            native->taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

            if(native->taskbarCreatedMessage == 0)
            {
                delete native;
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "RegisterWindowMessageW TaskbarCreated");
            }
            state.platform = native;
            native->window = CreateWindowExW(
                WS_EX_TOOLWINDOW,
                kTrayWindowClassName,
                L"GameWIP tray",
                WS_POPUP,
                0,
                0,
                0,
                0,
                nullptr,
                nullptr,
                GetModuleHandleW(nullptr),
                native);

            if(native->window == nullptr)
            {
                state.platform = nullptr;
                delete native;
                return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "CreateWindowExW tray message window");
            }
            state.open = true;

            // Publish the icon and tooltip after the callback window is ready.
            status = applyTray(state);

            if(!status.ok())
            {
                closeTrayBestEffort(state);
            }

            return status;
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status applyTray(TrayIconState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeApply))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }

        auto *native = static_cast<TrayNative *>(state.platform);

        if(native == nullptr || native->window == nullptr || state.icons.empty())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }
        const OwnedIconImage &selectedIcon = closestIcon(state.icons, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
        HICON newIcon = createIcon(selectedIcon);

        if(newIcon == nullptr)
        {
            return statusFromWin32(ErrorCode::NativeFailure, GetLastError(), "create tray icon");
        }

        try
        {
            // Convert the tooltip and prepare the native notification data.
            std::wstring tooltip;
            DWORD nativeCode = ERROR_SUCCESS;

            if(!utf8ToUtf16(state.tooltip, tooltip, nativeCode))
            {
                DestroyIcon(newIcon);
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = native->window;
            data.uID = kTrayNativeId;
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            data.uCallbackMessage = kTrayCallbackMessage;
            data.hIcon = newIcon;
            const std::size_t length = std::min<std::size_t>(tooltip.size(), ARRAYSIZE(data.szTip) - 1);
            std::copy_n(tooltip.data(), length, data.szTip);
            const DWORD operation = native->icon == nullptr ? NIM_ADD : NIM_MODIFY;

            // Publish the icon, then replace the retained native handle.
            if(Shell_NotifyIconW(operation, &data) == FALSE)
            {
                const DWORD errorCode = GetLastError();
                DestroyIcon(newIcon);
                return statusFromWin32(ErrorCode::NativeFailure, errorCode, "Shell_NotifyIconW tray icon");
            }

            if(native->icon == nullptr)
            {
                data.uVersion = NOTIFYICON_VERSION_4;
                native->version4 = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
            }

            if(native->icon != nullptr)
            {
                DestroyIcon(native->icon);
            }
            native->icon = newIcon;
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            DestroyIcon(newIcon);
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            DestroyIcon(newIcon);
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status closeTray(TrayIconState &state) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ShellNativeClose))
        {
            return IO::makeStatus(ErrorCode::NativeFailure);
        }

        auto *native = static_cast<TrayNative *>(state.platform);

        if(native == nullptr)
        {
            state.open = false;
            return IO::successStatus();
        }

        // Remove the shell entry before destroying its callback window and icon.
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = native->window;
        data.uID = kTrayNativeId;
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));

        if(native->window != nullptr)
        {
            DestroyWindow(native->window);
        }

        if(native->icon != nullptr)
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
} // namespace GameWIP::Desktop::Platform
