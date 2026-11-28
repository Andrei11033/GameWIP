/// @file accessibility_text.cpp
/// @brief Allocates retained UI Automation text ranges.

#include "desktop/platform/win32/internal/win32_accessibility_text_helpers.h"

namespace GameWIP::Desktop::Platform
{
    HRESULT makeTextRange(const UiaQuery &query, A::TextRange bounds, ITextRangeProvider **out) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;

        if(!query)
        {
            return kUnavailable;
        }

        return static_cast<UiaState *>(query.runtime->native.get())->range(query, bounds, out);
    }
} // namespace GameWIP::Desktop::Platform
