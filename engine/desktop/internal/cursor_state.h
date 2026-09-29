/// @file cursor_state.h
/// @brief Private immutable custom cursor resource state.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace GameWIP::Desktop
{
    class Cursor;
}

namespace GameWIP::Desktop::Detail
{
    /// @brief One native cursor handle paired with the DPI it was authored for.
    struct NativeCursorVariant
    {
        std::uint32_t intendedDpi = 0;
        void *handle = nullptr;
    };

    /// @brief Immutable custom cursor variants and their native resource lifetime.
    struct CursorState final
    {
        explicit CursorState(std::vector<NativeCursorVariant> nativeVariants) noexcept;
        ~CursorState() noexcept;

        CursorState(const CursorState &) = delete;
        CursorState &operator=(const CursorState &) = delete;
        CursorState(CursorState &&) = delete;
        CursorState &operator=(CursorState &&) = delete;

        /// @brief Selects the nearest authored variant, preferring the higher DPI on ties.
        [[nodiscard]] const NativeCursorVariant &variantForDpi(std::uint32_t dpi) const noexcept;

        std::vector<NativeCursorVariant> variants;
    };

    /// @brief Private construction/access bridge for the public Cursor wrapper.
    struct CursorAccess
    {
        /// @brief Creates a public Cursor wrapper around immutable native state.
        [[nodiscard]] static Cursor make(std::shared_ptr<const CursorState> state) noexcept;
        /// @brief Returns the immutable state retained by a public Cursor.
        [[nodiscard]] static const std::shared_ptr<const CursorState> &state(const Cursor &cursor) noexcept;
    };
} // namespace GameWIP::Desktop::Detail
