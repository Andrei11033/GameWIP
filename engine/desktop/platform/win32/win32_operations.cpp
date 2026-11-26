/// @file win32_operations.cpp
/// @brief Win32 ownership, identity, icon, and geometry operations for Window.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/win32_shell_helpers.h"
#include "desktop/platform/win32/internal/win32_unicode.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <algorithm>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Native geometry and icon helpers
    /// ------------------------------------------------------------

    namespace
    {
        /// @brief Changes the HWND owner while preserving Win32's zero-value error ambiguity.
        [[nodiscard]] GameWIP::Status::Types::Status setNativeParent(HWND window, HWND owner) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
            const DWORD nativeCode = GetLastError();

            if(previous == 0 && nativeCode != ERROR_SUCCESS)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, nativeCode, "set window owner");
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Converts a logical client rectangle into a DPI-scaled outer frame.
        [[nodiscard]] GameWIP::Status::Types::Status setOuterRect(WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept
        {
            const UINT dpi = dpiForWindow(state.platform->handle);
            const Types::PixelSize physicalSize = logicalToPhysicalSize(size, dpi);

            if(physicalSize.width == 0 || physicalSize.height == 0 ||
               physicalSize.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
               physicalSize.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "client size exceeds Win32 range at the effective DPI");
            }

            RECT outer{0, 0, static_cast<LONG>(physicalSize.width), static_cast<LONG>(physicalSize.height)};
            LONG_PTR styleValue = 0;
            GameWIP::Status::Types::Status status = queryWindowLong(state.platform->handle, GWL_STYLE, styleValue, "GetWindowLongPtrW geometry style");

            if(!status.ok())
            {
                return status;
            }

            LONG_PTR extendedStyleValue = 0;
            status = queryWindowLong(state.platform->handle, GWL_EXSTYLE, extendedStyleValue, "GetWindowLongPtrW geometry extended style");

            if(!status.ok())
            {
                return status;
            }

            const DWORD style = static_cast<DWORD>(styleValue);
            const DWORD extendedStyle = static_cast<DWORD>(extendedStyleValue);

            if(AdjustWindowRectExForDpi(&outer, style, FALSE, extendedStyle, dpi) == FALSE)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "AdjustWindowRectExForDpi");
            }
            const std::int64_t outerWidth = static_cast<std::int64_t>(outer.right) - outer.left;
            const std::int64_t outerHeight = static_cast<std::int64_t>(outer.bottom) - outer.top;
            const std::int64_t wideX = static_cast<std::int64_t>(position.x) + outer.left;
            const std::int64_t wideY = static_cast<std::int64_t>(position.y) + outer.top;

            if(wideX < std::numeric_limits<int>::min() || wideX > std::numeric_limits<int>::max() || wideY < std::numeric_limits<int>::min() ||
               wideY > std::numeric_limits<int>::max() || outerWidth <= 0 || outerWidth > std::numeric_limits<int>::max() || outerHeight <= 0 ||
               outerHeight > std::numeric_limits<int>::max())
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    GameWIP::Desktop::Internal::NativeCodeDomain::Win32Error,
                    "outer frame position exceeds Win32 range");
            }
            const int x = static_cast<int>(wideX);
            const int y = static_cast<int>(wideY);

            if(SetWindowPos(
                   state.platform->handle,
                   nullptr,
                   x,
                   y,
                   static_cast<int>(outerWidth),
                   static_cast<int>(outerHeight),
                   SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetWindowPos client rectangle");
            }
            return refreshCachedGeometry(state);
        }

        /// @brief Selects the supplied icon image with the smallest size distance.
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
    } // namespace

    /// ------------------------------------------------------------
    /// Ownership and content
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status setOwner(WindowState &state, Types::WindowId owner) noexcept
    {
        WindowState *ownerState = owner.isValid() ? resolveWindowId(owner) : nullptr;

        if(owner.isValid() &&
           (ownerState == nullptr || !ownerState->platform || ownerState->platform->ownerThreadId != state.platform->ownerThreadId))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        for(WindowState *ancestor = ownerState; ancestor != nullptr && ancestor->owner.isValid();)
        {
            if(ancestor->owner == state.id)
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
            }
            ancestor = resolveWindowId(ancestor->owner);
        }

        const Types::WindowId previous = state.owner;
        WindowState *previousOwnerState = previous.isValid() ? resolveWindowId(previous) : nullptr;
        HWND previousOwnerHandle = previousOwnerState != nullptr && previousOwnerState->platform ? previousOwnerState->platform->handle : nullptr;
        GameWIP::Status::Types::Status status = setNativeParent(state.platform->handle, ownerState != nullptr ? ownerState->platform->handle : nullptr);

        if(!status.ok())
        {
            return status;
        }

        state.owner = owner;
        status = applyStyle(state);

        if(!status.ok())
        {
            state.owner = previous;
            GameWIP::Status::Types::Status parentRollback = setNativeParent(state.platform->handle, previousOwnerHandle);
            GameWIP::Status::Types::Status styleRollback = applyStyle(state);

            if(!parentRollback.ok())
            {
                return parentRollback;
            }
            return styleRollback.ok() ? std::move(status) : std::move(styleRollback);
        }

        if(state.owner != previous)
        {
            routeEvent(
                state,
                Types::Events::OwnerChanged{
                    previous.isValid() ? std::optional<Types::WindowId>{previous} : std::nullopt,
                    state.owner.isValid() ? std::optional<Types::WindowId>{state.owner} : std::nullopt});
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setTitle(WindowState &state, std::string_view utf8Title) noexcept
    {
        try
        {
            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::TitleConversion))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::EncodingFailed);
            }
            std::wstring title;
            DWORD nativeCode = ERROR_SUCCESS;

            if(!utf8ToUtf16(utf8Title, title, nativeCode))
            {
                return statusFromWin32(unicodeConversionError(nativeCode, GameWIP::Status::Types::Code::InvalidArgument), nativeCode, "convert window title");
            }
            std::string cachedTitle(utf8Title);

            if(SetWindowTextW(state.platform->handle, title.c_str()) == FALSE)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetWindowTextW");
            }
            state.title.swap(cachedTitle);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    GameWIP::Status::Types::Status setIcon(WindowState &state, std::span<const Types::IconImageView> images) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::IconConversion))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NativeFailure);
        }

        const UINT dpi = dpiForWindow(state.platform->handle);
        const int largeWidth = GetSystemMetricsForDpi(SM_CXICON, dpi);
        const int largeHeight = GetSystemMetricsForDpi(SM_CYICON, dpi);
        const int smallWidth = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        const int smallHeight = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
        const Types::IconImageView &largeImage = closestIcon(images, largeWidth, largeHeight);
        HICON large = Win32::createIcon(largeImage.size, largeImage.rgba8);

        if(large == nullptr)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "CreateIconIndirect large");
        }

        const Types::IconImageView &smallImage = closestIcon(images, smallWidth, smallHeight);
        HICON small = Win32::createIcon(smallImage.size, smallImage.rgba8);

        if(small == nullptr)
        {
            DestroyIcon(large);
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "CreateIconIndirect small");
        }

        // The Window owns both handles after publication and releases them on replacement or close.
        SendMessageW(state.platform->handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        SendMessageW(state.platform->handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));

        if(state.platform->largeIcon != nullptr)
        {
            DestroyIcon(state.platform->largeIcon);
        }

        if(state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
        {
            DestroyIcon(state.platform->smallIcon);
        }
        state.platform->largeIcon = large;
        state.platform->smallIcon = small;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status clearIcon(WindowState &state) noexcept
    {
        SendMessageW(state.platform->handle, WM_SETICON, ICON_BIG, 0);
        SendMessageW(state.platform->handle, WM_SETICON, ICON_SMALL, 0);

        if(state.platform->largeIcon != nullptr)
        {
            DestroyIcon(state.platform->largeIcon);
        }

        if(state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
        {
            DestroyIcon(state.platform->smallIcon);
        }
        state.platform->largeIcon = nullptr;
        state.platform->smallIcon = nullptr;
        return GameWIP::Status::Types::Status{};
    }

    /// ------------------------------------------------------------
    /// Geometry and coordinate conversion
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status setClientSize(WindowState &state, Types::LogicalSize size) noexcept
    {
        return setOuterRect(state, state.clientPosition, size);
    }

    GameWIP::Status::Types::Status setClientPosition(WindowState &state, Types::ScreenPosition position) noexcept
    {
        return setOuterRect(state, position, state.clientSize);
    }

    GameWIP::Status::Types::Status setClientRect(WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept
    {
        return setOuterRect(state, position, size);
    }

    GameWIP::Status::Types::Status centerOn(WindowState &state, Types::Display::MonitorId monitor) noexcept
    {
        HMONITOR native = monitor.isValid() ? nativeMonitor(monitor) : MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);

        if(native == nullptr)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NotFound, ERROR_NOT_FOUND, "resolve center monitor");
        }

        MONITORINFO info{};
        info.cbSize = sizeof(info);

        if(GetMonitorInfoW(native, &info) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NotFound, GetLastError(), "resolve center monitor");
        }

        RECT frame{};

        if(GetWindowRect(state.platform->handle, &frame) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::StatFailed, GetLastError(), "GetWindowRect center");
        }
        const int width = frame.right - frame.left;
        const int height = frame.bottom - frame.top;

        if(SetWindowPos(
               state.platform->handle,
               nullptr,
               info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2,
               info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2,
               0,
               0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetWindowPos center");
        }
        return refreshCachedGeometry(state);
    }

    GameWIP::Status::Types::Status setSizeLimits(WindowState &state, const Types::SizeLimits &limits) noexcept
    {
        const Types::SizeLimits previous = state.sizeLimits;
        state.sizeLimits = limits;
        Types::LogicalSize clamped = state.clientSize;

        if(limits.minimum)
        {
            clamped.width = std::max(clamped.width, limits.minimum->width);
            clamped.height = std::max(clamped.height, limits.minimum->height);
        }

        if(limits.maximum)
        {
            clamped.width = std::min(clamped.width, limits.maximum->width);
            clamped.height = std::min(clamped.height, limits.maximum->height);
        }

        if(clamped != state.clientSize)
        {
            GameWIP::Status::Types::Status status = setClientSize(state, clamped);

            if(!status.ok())
            {
                state.sizeLimits = previous;
                return status;
            }
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setAspectRatio(WindowState &state, std::optional<Types::AspectRatio> ratio) noexcept
    {
        state.aspectRatio = ratio;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Result<Types::ScreenPosition> clientToScreen(const WindowState &state, Types::LogicalPosition position) noexcept
    {
        const UINT dpi = dpiForWindow(state.platform->handle);
        POINT point{};

        if(!logicalToPhysicalChecked(position.x, dpi, point.x) || !logicalToPhysicalChecked(position.y, dpi, point.y))
        {
            return GameWIP::Status::Types::Result<Types::ScreenPosition>::failure(
                GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "logical client position exceeds Win32 range at the effective DPI"));
        }

        if(ClientToScreen(state.platform->handle, &point) == FALSE)
        {
            return GameWIP::Status::Types::Result<Types::ScreenPosition>::failure(
                statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "ClientToScreen"));
        }

        return GameWIP::Status::Types::Result<Types::ScreenPosition>::success(Types::ScreenPosition{point.x, point.y});
    }

    GameWIP::Status::Types::Result<Types::LogicalPosition> screenToClient(const WindowState &state, Types::ScreenPosition position) noexcept
    {
        const UINT dpi = dpiForWindow(state.platform->handle);
        POINT point{position.x, position.y};

        if(ScreenToClient(state.platform->handle, &point) == FALSE)
        {
            return GameWIP::Status::Types::Result<Types::LogicalPosition>::failure(
                statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "ScreenToClient"));
        }

        return GameWIP::Status::Types::Result<Types::LogicalPosition>::success(
            Types::LogicalPosition{physicalToLogical(point.x, dpi), physicalToLogical(point.y, dpi)});
    }
} // namespace GameWIP::Desktop::Platform
