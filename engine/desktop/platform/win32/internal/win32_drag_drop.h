/// @file win32_drag_drop.h
/// @brief Private Win32 drag-effect conversion helpers.

#pragma once

#include "desktop/drag_drop.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace GameWIP::Desktop::Platform
{
    /// @brief Converts portable drag effects to the OLE bitmask.
    [[nodiscard]] DWORD nativeEffects(Types::DragDrop::Effect effects) noexcept;

    /// @brief Converts an OLE effect bitmask to the portable effect enum.
    [[nodiscard]] Types::DragDrop::Effect portableEffects(DWORD effects) noexcept;
} // namespace GameWIP::Desktop::Platform
