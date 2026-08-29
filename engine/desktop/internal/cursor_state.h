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
    struct NativeCursorVariant
    {
        std::uint32_t intendedDpi = 0;
        void *handle = nullptr;
    };

    struct CursorState final
    {
        explicit CursorState(std::vector<NativeCursorVariant> nativeVariants) noexcept;
        ~CursorState() noexcept;

        CursorState(const CursorState &) = delete;
        CursorState &operator=(const CursorState &) = delete;
        CursorState(CursorState &&) = delete;
        CursorState &operator=(CursorState &&) = delete;

        [[nodiscard]] const NativeCursorVariant &variantForDpi(std::uint32_t dpi) const noexcept;

        std::vector<NativeCursorVariant> variants;
    };

    struct CursorAccess
    {
        [[nodiscard]] static Cursor make(std::shared_ptr<const CursorState> state) noexcept;
        [[nodiscard]] static const std::shared_ptr<const CursorState> &state(const Cursor &cursor) noexcept;
    };
} // namespace GameWIP::Desktop::Detail
