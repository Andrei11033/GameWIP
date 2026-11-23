/// @file dialogs.cpp
/// @brief Shared text and owner helpers for Win32 dialogs.

#include "desktop/platform/win32/internal/win32_dialog_helpers.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/win32/internal/win32_unicode.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <new>

namespace GameWIP::Desktop::Platform::Win32
{
    HWND ownerHandle(Window *owner) noexcept
    {
        if(owner == nullptr)
        {
            return nullptr;
        }

        Internal::WindowState *state = Internal::WindowAccess::state(*owner);
        return state != nullptr ? static_cast<HWND>(nativeHandle(*state).window) : nullptr;
    }

    IO::Types::Status convertText(std::string_view text, std::wstring &output) noexcept
    {
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogConversion))
            {
                return IO::makeStatus(IO::Types::ErrorCode::EncodingFailed);
            }
#endif
            DWORD nativeCode = ERROR_SUCCESS;

            if(!utf8ToUtf16(text, output, nativeCode))
            {
                return IO::makeStatus(unicodeConversionError(nativeCode, IO::Types::ErrorCode::EncodingFailed), nativeCode);
            }
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::Desktop::Platform::Win32
