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

    /// @brief Immutable shared owner of eagerly materialized native cursor images.
    /// @details Copies share the same native resources. A default-constructed or failed result
    /// is invalid and owns no native cursor.
    class GAMEWIP_WINDOW_EXPORT Cursor final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Constructs an invalid cursor.
        Cursor() noexcept;
        /// @brief Shares another cursor's immutable native resources.
        Cursor(const Cursor &) noexcept;
        /// @brief Replaces this cursor with shared ownership of another cursor's resources.
        Cursor &operator=(const Cursor &) noexcept;
        /// @brief Transfers a cursor handle without duplicating native resources.
        Cursor(Cursor &&) noexcept;
        /// @brief Replaces this cursor by transferring another cursor handle.
        Cursor &operator=(Cursor &&) noexcept;
        /// @brief Releases this handle's shared ownership.
        ~Cursor() noexcept;
        /// @}

        /// @brief Reports whether this handle owns at least one native cursor variant.
        /// @return true when the cursor may be selected on a Window.
        [[nodiscard]] bool isValid() const noexcept;

    private:
        friend struct Detail::CursorAccess;

        explicit Cursor(std::shared_ptr<const Detail::CursorState> state) noexcept;

        std::shared_ptr<const Detail::CursorState> state_;
    };
} // namespace GameWIP::Window

namespace GameWIP::Window::Types::Cursor
{
    /// @brief Physical-pixel hotspot coordinate within a cursor image.
    struct PixelPosition
    {
        std::uint32_t x = 0; ///< Horizontal pixel coordinate from the left edge.
        std::uint32_t y = 0; ///< Vertical pixel coordinate from the top edge.

        /// @brief Compares both physical-pixel coordinates.
        [[nodiscard]] friend bool operator==(const PixelPosition &, const PixelPosition &) noexcept = default;
    };

    /// @brief Borrowed RGBA8 pixels for one physical-size cursor variant.
    /// @details Rows are top-to-bottom and may contain trailing padding. Pixel memory is copied
    /// during createCursor() and is never retained.
    struct ImageView
    {
        PixelSize size;                   ///< Physical width and height in pixels.
        PixelPosition hotspot;            ///< In-bounds physical-pixel hotspot.
        std::uint32_t intendedDpi = 96;   ///< Nonzero DPI at which this variant is intended for use.
        std::size_t rowStrideBytes = 0;   ///< Bytes per source row, or zero for tightly packed RGBA8.
        std::span<const std::byte> rgba8; ///< Exact top-to-bottom straight-alpha RGBA8 payload.
    };

    /// @brief Result of validating and materializing custom cursor variants.
    struct CreateResult
    {
        IO::Types::Status status;       ///< Success or the validation/native creation failure.
        GameWIP::Window::Cursor cursor; ///< Valid shared cursor only when status reports success.
    };
} // namespace GameWIP::Window::Types::Cursor

namespace GameWIP::Window
{
    /// @name Custom cursor operations
    /// @{

    /// @brief Validates and eagerly materializes one application-provided cursor image.
    /// @param image RGBA8 image and physical-pixel hotspot to materialize.
    /// @return A valid shared Cursor on success.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Cursor::CreateResult createCursor(const Types::Cursor::ImageView &image) noexcept;

    /// @brief Validates and eagerly materializes application-provided cursor variants.
    /// @param variants Non-empty set containing one RGBA8 image per unique intended DPI.
    /// @return A valid shared Cursor on success; no partial resource is published on failure.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Cursor::CreateResult createCursor(std::span<const Types::Cursor::ImageView> variants) noexcept;

    /// @brief Selects a custom cursor for an open Window on its owner thread.
    /// @param window Target Window whose standard CursorShape remains the cached fallback.
    /// @param cursor Valid custom cursor resource to retain lazily for the Window.
    /// @return Success, or a status describing validation, thread, allocation, or native failure.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status setCursor(Window &window, const Cursor &cursor) noexcept;

    /// @brief Reports whether an open owner-thread Window currently retains a custom cursor.
    /// @param window Window to inspect.
    /// @return true only while a custom cursor override is bound to the native Window.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasCustomCursor(const Window &window) noexcept;
    /// @}
} // namespace GameWIP::Window
