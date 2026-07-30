/// @file win32_operations.cpp
/// @brief Win32 ownership, identity, icon, and geometry operations for Window.

#include "window/platform/win32/internal/win32_window_backend.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        [[nodiscard]] IO::Types::Status setNativeParent(HWND window, HWND owner) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
            const DWORD nativeCode = GetLastError();
            if (previous == 0 && nativeCode != ERROR_SUCCESS)
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode, "set window owner");
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status setOuterRect(
            WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept
        {
            const UINT dpi = dpiForWindow(state.platform->handle);
            const Types::PixelSize physicalSize = logicalToPhysicalSize(size, dpi);
            if (physicalSize.width == 0 || physicalSize.height == 0 ||
                physicalSize.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
                physicalSize.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
            {
                return IO::makeStatus(
                    IO::Types::ErrorCode::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "client size exceeds Win32 range at the effective DPI");
            }
            RECT outer{0, 0, static_cast<LONG>(physicalSize.width), static_cast<LONG>(physicalSize.height)};
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(state.platform->handle, GWL_STYLE));
            const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(state.platform->handle, GWL_EXSTYLE));
            if (AdjustWindowRectExForDpi(&outer, style, FALSE, extendedStyle, dpi) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "AdjustWindowRectExForDpi");
            const std::int64_t outerWidth = static_cast<std::int64_t>(outer.right) - outer.left;
            const std::int64_t outerHeight = static_cast<std::int64_t>(outer.bottom) - outer.top;
            const std::int64_t wideX = static_cast<std::int64_t>(position.x) + outer.left;
            const std::int64_t wideY = static_cast<std::int64_t>(position.y) + outer.top;
            if (wideX < std::numeric_limits<int>::min() || wideX > std::numeric_limits<int>::max() ||
                wideY < std::numeric_limits<int>::min() || wideY > std::numeric_limits<int>::max() ||
                outerWidth <= 0 || outerWidth > std::numeric_limits<int>::max() ||
                outerHeight <= 0 || outerHeight > std::numeric_limits<int>::max())
            {
                return IO::makeStatus(
                    IO::Types::ErrorCode::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "outer frame position exceeds Win32 range");
            }
            const int x = static_cast<int>(wideX);
            const int y = static_cast<int>(wideY);
            if (SetWindowPos(
                    state.platform->handle,
                    nullptr,
                    x,
                    y,
                    static_cast<int>(outerWidth),
                    static_cast<int>(outerHeight),
                    SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowPos client rectangle");
            }
            return refreshCachedGeometry(state);
        }

        [[nodiscard]] const Types::IconImageView &closestIcon(
            std::span<const Types::IconImageView> images,
            int desiredWidth,
            int desiredHeight) noexcept
        {
            return *std::min_element(
                images.begin(),
                images.end(),
                [desiredWidth, desiredHeight](const auto &left, const auto &right)
                {
                    const auto distance = [desiredWidth, desiredHeight](const Types::IconImageView &image)
                    {
                        return std::llabs(static_cast<long long>(image.size.width) - desiredWidth) +
                               std::llabs(static_cast<long long>(image.size.height) - desiredHeight);
                    };
                    return distance(left) < distance(right);
                });
        }

        [[nodiscard]] HICON createIcon(const Types::IconImageView &image) noexcept
        {
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

            void *pixels = nullptr;
            HDC screen = GetDC(nullptr);
            if (screen == nullptr)
                return nullptr;
            HBITMAP color = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO *>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);
            if (color == nullptr || pixels == nullptr)
            {
                if (color != nullptr)
                    DeleteObject(color);
                return nullptr;
            }

            auto *destination = static_cast<std::byte *>(pixels);
            for (std::size_t offset = 0; offset < image.rgba8.size(); offset += 4)
            {
                const std::uint8_t red = std::to_integer<std::uint8_t>(image.rgba8[offset]);
                const std::uint8_t green = std::to_integer<std::uint8_t>(image.rgba8[offset + 1]);
                const std::uint8_t blue = std::to_integer<std::uint8_t>(image.rgba8[offset + 2]);
                const std::uint8_t alpha = std::to_integer<std::uint8_t>(image.rgba8[offset + 3]);
                destination[offset] = static_cast<std::byte>(blue);
                destination[offset + 1] = static_cast<std::byte>(green);
                destination[offset + 2] = static_cast<std::byte>(red);
                destination[offset + 3] = static_cast<std::byte>(alpha);
            }

            HBITMAP mask = CreateBitmap(static_cast<int>(image.size.width), static_cast<int>(image.size.height), 1, 1, nullptr);
            if (mask == nullptr)
            {
                DeleteObject(color);
                return nullptr;
            }
            ICONINFO info{TRUE, 0, 0, mask, color};
            HICON icon = CreateIconIndirect(&info);
            DeleteObject(mask);
            DeleteObject(color);
            return icon;
        }
    } // namespace

    IO::Types::Status setOwner(WindowState &state, Types::WindowId owner) noexcept
    {
        WindowState *ownerState = owner.valid() ? resolveWindowId(owner) : nullptr;
        if (owner.valid() && (ownerState == nullptr || !ownerState->platform || ownerState->platform->ownerThreadId != state.platform->ownerThreadId))
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        for (WindowState *ancestor = ownerState; ancestor != nullptr && ancestor->owner.valid();)
        {
            if (ancestor->owner == state.id)
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            ancestor = resolveWindowId(ancestor->owner);
        }
        const Types::WindowId previous = state.owner;
        WindowState *previousOwnerState = previous.valid() ? resolveWindowId(previous) : nullptr;
        HWND previousOwnerHandle =
            previousOwnerState != nullptr && previousOwnerState->platform ? previousOwnerState->platform->handle : nullptr;
        IO::Types::Status status = setNativeParent(
            state.platform->handle,
            ownerState != nullptr ? ownerState->platform->handle : nullptr);
        if (!status.ok())
            return status;
        state.owner = owner;
        status = applyStyle(state);
        if (!status.ok())
        {
            state.owner = previous;
            static_cast<void>(setNativeParent(state.platform->handle, previousOwnerHandle));
            static_cast<void>(applyStyle(state));
            return status;
        }
        if (state.owner != previous)
            routeEvent(state, Types::OwnerChangedEvent{previous, state.owner});
        return IO::successStatus();
    }

    IO::Types::Status setTitle(WindowState &state, std::string_view utf8Title) noexcept
    {
        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::TitleConversion))
                return IO::makeStatus(IO::Types::ErrorCode::EncodingFailed);
            std::wstring title;
            DWORD nativeCode = ERROR_SUCCESS;
            if (!utf8ToUtf16(utf8Title, title, nativeCode))
                return statusFromWin32(IO::Types::ErrorCode::EncodingFailed, nativeCode, "convert window title");
            std::string cachedTitle(utf8Title);
            if (SetWindowTextW(state.platform->handle, title.c_str()) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowTextW");
            state.title.swap(cachedTitle);
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

    IO::Types::Status setIcon(WindowState &state, std::span<const Types::IconImageView> images) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::IconConversion))
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
        const UINT dpi = dpiForWindow(state.platform->handle);
        const int largeWidth = GetSystemMetricsForDpi(SM_CXICON, dpi);
        const int largeHeight = GetSystemMetricsForDpi(SM_CYICON, dpi);
        const int smallWidth = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        const int smallHeight = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
        HICON large = createIcon(closestIcon(images, largeWidth, largeHeight));
        if (large == nullptr)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "CreateIconIndirect large");
        HICON small = createIcon(closestIcon(images, smallWidth, smallHeight));
        if (small == nullptr)
        {
            DestroyIcon(large);
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "CreateIconIndirect small");
        }
        SendMessageW(state.platform->handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        SendMessageW(state.platform->handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        if (state.platform->largeIcon != nullptr)
            DestroyIcon(state.platform->largeIcon);
        if (state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
            DestroyIcon(state.platform->smallIcon);
        state.platform->largeIcon = large;
        state.platform->smallIcon = small;
        return IO::successStatus();
    }

    IO::Types::Status clearIcon(WindowState &state) noexcept
    {
        SendMessageW(state.platform->handle, WM_SETICON, ICON_BIG, 0);
        SendMessageW(state.platform->handle, WM_SETICON, ICON_SMALL, 0);
        if (state.platform->largeIcon != nullptr)
            DestroyIcon(state.platform->largeIcon);
        if (state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
            DestroyIcon(state.platform->smallIcon);
        state.platform->largeIcon = nullptr;
        state.platform->smallIcon = nullptr;
        return IO::successStatus();
    }

    IO::Types::Status setClientSize(WindowState &state, Types::LogicalSize size) noexcept
    {
        return setOuterRect(state, state.clientPosition, size);
    }

    IO::Types::Status setClientPosition(WindowState &state, Types::ScreenPosition position) noexcept
    {
        return setOuterRect(state, position, state.clientSize);
    }

    IO::Types::Status setClientRect(WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept
    {
        return setOuterRect(state, position, size);
    }

    IO::Types::Status centerOn(WindowState &state, Types::MonitorId monitor) noexcept
    {
        HMONITOR native = monitor.valid() ? nativeMonitor(monitor) : MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (native == nullptr || GetMonitorInfoW(native, &info) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NotFound, GetLastError(), "resolve center monitor");
        RECT frame{};
        if (GetWindowRect(state.platform->handle, &frame) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "GetWindowRect center");
        const int width = frame.right - frame.left;
        const int height = frame.bottom - frame.top;
        if (SetWindowPos(
                state.platform->handle,
                nullptr,
                info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2,
                info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2,
                0,
                0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowPos center");
        }
        return refreshCachedGeometry(state);
    }

    IO::Types::Status setSizeLimits(WindowState &state, const Types::SizeLimits &limits) noexcept
    {
        const Types::SizeLimits previous = state.sizeLimits;
        state.sizeLimits = limits;
        Types::LogicalSize clamped = state.clientSize;
        if (limits.minimum)
        {
            clamped.width = std::max(clamped.width, limits.minimum->width);
            clamped.height = std::max(clamped.height, limits.minimum->height);
        }
        if (limits.maximum)
        {
            clamped.width = std::min(clamped.width, limits.maximum->width);
            clamped.height = std::min(clamped.height, limits.maximum->height);
        }
        if (clamped != state.clientSize)
        {
            IO::Types::Status status = setClientSize(state, clamped);
            if (!status.ok())
            {
                state.sizeLimits = previous;
                return status;
            }
        }
        return IO::successStatus();
    }

    IO::Types::Status setAspectRatio(WindowState &state, std::optional<Types::AspectRatio> ratio) noexcept
    {
        state.aspectRatio = ratio;
        return IO::successStatus();
    }

    Types::ScreenPositionResult clientToScreen(const WindowState &state, Types::LogicalPosition position) noexcept
    {
        const UINT dpi = dpiForWindow(state.platform->handle);
        POINT point{};
        if (!logicalToPhysicalChecked(position.x, dpi, point.x) || !logicalToPhysicalChecked(position.y, dpi, point.y))
        {
            return {
                .status = IO::makeStatus(
                    IO::Types::ErrorCode::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "logical client position exceeds Win32 range at the effective DPI")};
        }
        if (ClientToScreen(state.platform->handle, &point) == FALSE)
            return {.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClientToScreen")};
        return {.status = IO::successStatus(), .position = {point.x, point.y}};
    }

    Types::LogicalPositionResult screenToClient(const WindowState &state, Types::ScreenPosition position) noexcept
    {
        const UINT dpi = dpiForWindow(state.platform->handle);
        POINT point{position.x, position.y};
        if (ScreenToClient(state.platform->handle, &point) == FALSE)
            return {.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ScreenToClient")};
        return {.status = IO::successStatus(), .position = {physicalToLogical(point.x, dpi), physicalToLogical(point.y, dpi)}};
    }
} // namespace GameWIP::Window::Detail::Platform
