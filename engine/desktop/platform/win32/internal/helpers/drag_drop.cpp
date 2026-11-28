/// @file drag_drop.cpp
/// @brief OLE effect conversion for Desktop's Win32 drag/drop backend.

#include "desktop/platform/win32/internal/win32_drag_drop.h"

#include <ole2.h>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// OLE effect conversion
    /// ------------------------------------------------------------

    DWORD nativeEffects(Types::DragDrop::Effect effects) noexcept
    {
        DWORD result = 0;

        if((effects & Types::DragDrop::Effect::Copy) != Types::DragDrop::Effect::None)
        {
            result |= DROPEFFECT_COPY;
        }

        if((effects & Types::DragDrop::Effect::Move) != Types::DragDrop::Effect::None)
        {
            result |= DROPEFFECT_MOVE;
        }

        if((effects & Types::DragDrop::Effect::Link) != Types::DragDrop::Effect::None)
        {
            result |= DROPEFFECT_LINK;
        }

        return result;
    }

    Types::DragDrop::Effect portableEffects(DWORD effects) noexcept
    {
        Types::DragDrop::Effect result = Types::DragDrop::Effect::None;

        if(effects & DROPEFFECT_COPY)
        {
            result |= Types::DragDrop::Effect::Copy;
        }

        if(effects & DROPEFFECT_MOVE)
        {
            result |= Types::DragDrop::Effect::Move;
        }

        if(effects & DROPEFFECT_LINK)
        {
            result |= Types::DragDrop::Effect::Link;
        }

        return result;
    }
} // namespace GameWIP::Desktop::Platform
