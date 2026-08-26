/// @file cursor.h
/// @brief Optional application-provided native cursor image API.

#pragma once

#include "io/io.h"
#include "window/types.h"
#include "window/window_export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace GameWIP::Window
{
    class Window;

    namespace Detail
    {
        struct CursorAccess;
        struct CursorState;
    } // namespace Detail

    class GAMEWIP_WINDOW_EXPORT Cursor final
    {
    public:
        Cursor() noexcept;
        Cursor(const Cursor &) noexcept;
        Cursor &operator=(const Cursor &) noexcept;
        Cursor(Cursor &&) noexcept;
        Cursor &operator=(Cursor &&) noexcept;
        ~Cursor() noexcept;

        [[nodiscard]] bool isValid() const noexcept;

    private:
        friend struct Detail::CursorAccess;

        explicit Cursor(std::shared_ptr<const Detail::CursorState> state) noexcept;

        std::shared_ptr<const Detail::CursorState> state_;
    };
} // namespace GameWIP::Window

namespace GameWIP::Window::Types::Cursor
{
    struct PixelPosition
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
    };

    struct ImageView
    {
        PixelSize size;
        PixelPosition hotspot;
        std::uint32_t intendedDpi = 96;
        std::size_t rowStrideBytes = 0;
        std::span<const std::byte> rgba8;
    };

    struct CreateResult
    {
        IO::Types::Status status;
        GameWIP::Window::Cursor cursor;
    };
} // namespace GameWIP::Window::Types::Cursor

namespace GameWIP::Window
{
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Cursor::CreateResult createCursor(std::span<const Types::Cursor::ImageView> variants) noexcept;

    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status setCursor(Window &window, const Cursor &cursor) noexcept;

    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasCustomCursor(const Window &window) noexcept;
} // namespace GameWIP::Window
