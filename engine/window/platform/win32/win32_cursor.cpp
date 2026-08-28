/// @file win32_cursor.cpp
/// @brief Win32 custom cursor materialization, lazy Window binding, and DPI selection.

#include "window/platform/win32/internal/win32_window_backend.h"

#include "window/internal/cursor_platform.h"
#include "window/internal/cursor_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace GameWIP::Window::Detail::Platform
{
    // ------------------------------------------------------------
    // Cursor image and binding helpers
    // ------------------------------------------------------------
    namespace
    {
        inline constexpr wchar_t kCursorBindingProperty[] = L"GameWIP.Window.CustomCursorBinding.v1";

        struct CursorBinding final
        {
            std::shared_ptr<const CursorState> state;
            std::uint32_t selectedDpi = 0;
        };

        [[nodiscard]] CursorBinding *cursorBinding(HWND window) noexcept
        {
            return static_cast<CursorBinding *>(GetPropW(window, kCursorBindingProperty));
        }

        [[nodiscard]] bool cursorVisible(const WindowState &state) noexcept
        {
            return state.cursorInside && state.cursorMode != Types::CursorMode::Hidden && state.cursorMode != Types::CursorMode::HiddenConfined &&
                   state.cursorMode != Types::CursorMode::Relative;
        }

        [[nodiscard]] IO::Types::Status nativeCursorFailure(DWORD nativeCode, const char *operation) noexcept
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode == ERROR_SUCCESS ? ERROR_GEN_FAILURE : nativeCode, operation);
        }

        [[nodiscard]] IO::Types::Status createNativeCursor(const Types::Cursor::ImageView &image, HCURSOR &cursor) noexcept
        {
            cursor = nullptr;

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

            SetLastError(ERROR_SUCCESS);
            HDC screen = GetDC(nullptr);
            if (screen == nullptr)
                return nativeCursorFailure(GetLastError(), "GetDC custom cursor");

            void *pixels = nullptr;
            HBITMAP color = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO *>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            const DWORD colorError = GetLastError();
            ReleaseDC(nullptr, screen);
            if (color == nullptr || pixels == nullptr)
            {
                if (color != nullptr)
                    DeleteObject(color);
                return nativeCursorFailure(colorError, "CreateDIBSection custom cursor");
            }

            const std::size_t packedRowBytes = static_cast<std::size_t>(image.size.width) * 4;
            const std::size_t sourceStride = image.rowStrideBytes == 0 ? packedRowBytes : image.rowStrideBytes;
            const std::size_t destinationSize = packedRowBytes * static_cast<std::size_t>(image.size.height);
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            std::span<std::byte> destination(static_cast<std::byte *>(pixels), destinationSize);
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
            for (std::size_t row = 0; row < image.size.height; ++row)
            {
                const std::size_t sourceOffset = row * sourceStride;
                const std::size_t destinationOffset = row * packedRowBytes;
                for (std::size_t column = 0; column < packedRowBytes; column += 4)
                {
                    destination[destinationOffset + column] = image.rgba8[sourceOffset + column + 2];
                    destination[destinationOffset + column + 1] = image.rgba8[sourceOffset + column + 1];
                    destination[destinationOffset + column + 2] = image.rgba8[sourceOffset + column];
                    destination[destinationOffset + column + 3] = image.rgba8[sourceOffset + column + 3];
                }
            }

            SetLastError(ERROR_SUCCESS);
            HBITMAP mask = CreateBitmap(static_cast<int>(image.size.width), static_cast<int>(image.size.height), 1, 1, nullptr);
            if (mask == nullptr)
            {
                const DWORD maskError = GetLastError();
                DeleteObject(color);
                return nativeCursorFailure(maskError, "CreateBitmap custom cursor mask");
            }

            HDC maskDc = CreateCompatibleDC(nullptr);
            HGDIOBJ previousMask = maskDc != nullptr ? SelectObject(maskDc, mask) : nullptr;
            if (maskDc == nullptr || previousMask == nullptr ||
                PatBlt(maskDc, 0, 0, static_cast<int>(image.size.width), static_cast<int>(image.size.height), BLACKNESS) == FALSE)
            {
                const DWORD maskFillError = GetLastError();
                if (maskDc != nullptr)
                {
                    if (previousMask != nullptr)
                        SelectObject(maskDc, previousMask);
                    DeleteDC(maskDc);
                }
                DeleteObject(mask);
                DeleteObject(color);
                return nativeCursorFailure(maskFillError, "initialize custom cursor mask");
            }
            SelectObject(maskDc, previousMask);
            DeleteDC(maskDc);

            ICONINFO info{};
            info.fIcon = FALSE;
            info.xHotspot = image.hotspot.x;
            info.yHotspot = image.hotspot.y;
            info.hbmMask = mask;
            info.hbmColor = color;
            SetLastError(ERROR_SUCCESS);
            cursor = static_cast<HCURSOR>(CreateIconIndirect(&info));
            const DWORD cursorError = GetLastError();
            DeleteObject(mask);
            DeleteObject(color);
            if (cursor == nullptr)
                return nativeCursorFailure(cursorError, "CreateIconIndirect custom cursor");

            Detail::recordCustomCursorCreated();
            return IO::successStatus();
        }
    } // namespace

    IO::Types::Status createNativeCursorVariants(
        std::span<const Types::Cursor::ImageView> images,
        std::vector<NativeCursorVariant> &variants) noexcept
    {
        for (const Types::Cursor::ImageView &image : images)
        {
            if (Detail::consumeCursorNativeCreationFailure())
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, ERROR_GEN_FAILURE, "injected custom cursor creation failure");

            HCURSOR cursor = nullptr;
            IO::Types::Status status = createNativeCursor(image, cursor);
            if (!status.ok())
                return status;
            variants.push_back({image.intendedDpi, cursor});
        }
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Native cursor resources
    // ------------------------------------------------------------
    void destroyNativeCursorVariants(std::span<const NativeCursorVariant> variants) noexcept
    {
        for (const NativeCursorVariant &variant : variants)
        {
            if (variant.handle != nullptr)
            {
                static_cast<void>(DestroyCursor(static_cast<HCURSOR>(variant.handle)));
                Detail::recordCustomCursorDestroyed();
            }
        }
    }

    // ------------------------------------------------------------
    // Window cursor bindings
    // ------------------------------------------------------------
    IO::Types::Status setCustomCursor(WindowState &window, std::shared_ptr<const CursorState> cursor) noexcept
    {
        try
        {
            const std::uint32_t dpi = dpiForWindow(window.platform->handle);
            const NativeCursorVariant &variant = cursor->variantForDpi(dpi);
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
                throw std::bad_alloc{};
            auto replacement = std::make_unique<CursorBinding>(CursorBinding{std::move(cursor), variant.intendedDpi});
            CursorBinding *previous = cursorBinding(window.platform->handle);

            if (Detail::consumeFailure(TestHooks::FailurePoint::CursorBinding))
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, ERROR_GEN_FAILURE, "injected custom cursor binding failure");
            SetLastError(ERROR_SUCCESS);
            if (SetPropW(window.platform->handle, kCursorBindingProperty, replacement.get()) == FALSE)
                return nativeCursorFailure(GetLastError(), "SetPropW custom cursor binding");

            window.platform->cursor = static_cast<HCURSOR>(variant.handle);
            if (cursorVisible(window))
                SetCursor(window.platform->cursor);
            CursorBinding *published = replacement.release();
            delete previous;
            if (published == nullptr)
                return IO::makeStatus(IO::Types::ErrorCode::Unknown);
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

    bool hasCustomCursor(const WindowState &window) noexcept
    {
        return window.platform && window.platform->handle != nullptr && cursorBinding(window.platform->handle) != nullptr;
    }

    std::uint32_t customCursorBindingDpi(const WindowState &window) noexcept
    {
        const CursorBinding *binding = window.platform && window.platform->handle != nullptr ? cursorBinding(window.platform->handle) : nullptr;
        return binding != nullptr ? binding->selectedDpi : 0;
    }

    void releaseCustomCursorBinding(HWND window) noexcept
    {
        if (window == nullptr)
            return;
        CursorBinding *binding = cursorBinding(window);
        if (binding == nullptr)
            return;
        static_cast<void>(RemovePropW(window, kCursorBindingProperty));
        delete binding;
    }

    void refreshCustomCursorForDpi(WindowState &state, std::uint32_t dpi) noexcept
    {
        CursorBinding *binding = cursorBinding(state.platform->handle);
        if (binding == nullptr)
            return;
        const NativeCursorVariant &variant = binding->state->variantForDpi(dpi);
        binding->selectedDpi = variant.intendedDpi;
        state.platform->cursor = static_cast<HCURSOR>(variant.handle);
        if (cursorVisible(state))
            SetCursor(state.platform->cursor);
    }

    IO::Types::Status replaceCustomCursorWithSystem(WindowState &state, HCURSOR cursor) noexcept
    {
        CursorBinding *binding = cursorBinding(state.platform->handle);
        if (binding != nullptr)
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::CursorBinding))
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, ERROR_GEN_FAILURE, "injected custom cursor removal failure");
            SetLastError(ERROR_SUCCESS);
            HANDLE removed = RemovePropW(state.platform->handle, kCursorBindingProperty);
            if (removed != binding)
                return nativeCursorFailure(GetLastError(), "RemovePropW custom cursor binding");
        }

        state.platform->cursor = cursor;
        if (cursorVisible(state))
            SetCursor(cursor);
        delete binding;
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Validation inspection
    // ------------------------------------------------------------
    NativeCursorSnapshot inspectNativeCursor(const NativeCursorVariant &variant) noexcept
    {
        ICONINFO info{};
        if (variant.handle == nullptr || GetIconInfo(static_cast<HCURSOR>(variant.handle), &info) == FALSE)
            return {};

        NativeCursorSnapshot snapshot;
        snapshot.hotspotX = info.xHotspot;
        snapshot.hotspotY = info.yHotspot;
        try
        {
            BITMAP bitmap{};
            if (info.hbmColor != nullptr && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap) && bitmap.bmWidth > 0 &&
                bitmap.bmHeight > 0)
            {
                BITMAPV5HEADER header{};
                header.bV5Size = sizeof(header);
                header.bV5Width = bitmap.bmWidth;
                header.bV5Height = -bitmap.bmHeight;
                header.bV5Planes = 1;
                header.bV5BitCount = 32;
                header.bV5Compression = BI_RGB;
                const std::size_t byteCount = static_cast<std::size_t>(bitmap.bmWidth) * static_cast<std::size_t>(bitmap.bmHeight) * 4;
                std::vector<std::byte> pixels(byteCount);
                HDC screen = GetDC(nullptr);
                const int rows = screen != nullptr ? GetDIBits(
                                                         screen,
                                                         info.hbmColor,
                                                         0,
                                                         static_cast<UINT>(bitmap.bmHeight),
                                                         pixels.data(),
                                                         reinterpret_cast<BITMAPINFO *>(&header),
                                                         DIB_RGB_COLORS)
                                                   : 0;
                if (screen != nullptr)
                    ReleaseDC(nullptr, screen);
                if (rows == bitmap.bmHeight)
                {
                    std::copy_n(pixels.begin(), snapshot.firstBgraPixel.size(), snapshot.firstBgraPixel.begin());
                    snapshot.valid = true;
                }
            }
        }
        catch (...)
        {
            snapshot.valid = false;
        }
        if (info.hbmMask != nullptr)
            DeleteObject(info.hbmMask);
        if (info.hbmColor != nullptr)
            DeleteObject(info.hbmColor);
        return snapshot;
    }
} // namespace GameWIP::Window::Detail::Platform
