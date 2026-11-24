/// @file xinput.cpp
/// @brief Win32 XInput loading and value helpers.

#include "base/platform/win32/dynamic_library.h"
#include "input/platform/win32/internal/win32_attachment_state.h"

#include <array>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// XInput helpers
    /// ------------------------------------------------------------

    [[nodiscard]] float xInputTrigger(BYTE value) noexcept
    {
        return static_cast<float>(value) / 255.0F;
    }

    [[nodiscard]] float xInputAxis(SHORT value) noexcept
    {
        return value < 0 ? static_cast<float>(value) / 32768.0F : static_cast<float>(value) / 32767.0F;
    }

    void releaseXInput(AttachmentState &attachment) noexcept
    {
        if(attachment.xinputModule != nullptr)
        {
            FreeLibrary(attachment.xinputModule);
            attachment.xinputModule = nullptr;
        }
        attachment.xinputGetState = nullptr;
        attachment.xinputLoadAttempted = false;
        attachment.xinputFailureReported = false;
        attachment.xinputPackets.fill(0);
        attachment.xinputHasPackets.fill(false);
    }

    void loadXInput(AttachmentState &attachment) noexcept
    {
        if(attachment.xinputLoadAttempted)
        {
            return;
        }
        attachment.xinputLoadAttempted = true;
        constexpr std::array<const wchar_t *, 3> libraries{L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};

        for(const wchar_t *name : libraries)
        {
            HMODULE module = LoadLibraryW(name);
            if(module == nullptr)
            {
                continue;
            }

            const auto function = GameWIP::Base::Platform::Win32::loadProcedure<AttachmentState::XInputGetStateFunction>(module, "XInputGetState");
            if(function != nullptr)
            {
                attachment.xinputModule = module;
                attachment.xinputGetState = function;
                return;
            }
            FreeLibrary(module);
        }
    }
} // namespace GameWIP::Input::Platform::Win32
