#include "debug/assert/internal/assert_platform.h"

#include <array>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <commctrl.h>

namespace
{
    bool popupsSuppressed() noexcept
    {
        char value[2]{};
        const DWORD size = GetEnvironmentVariableA("GAMEWIP_ASSERT_SUPPRESS_POPUP", value, static_cast<DWORD>(sizeof(value)));
        return size == 1 && value[0] == '1';
    }

    using FailureAction = GameWIP::Debug::Assert::FailureAction;

    template <std::size_t Capacity>
    void utf8ToWide(std::string_view text, std::array<wchar_t, Capacity> &output) noexcept
    {
        static_assert(Capacity > 0);
        output[0] = L'\0';
        if constexpr (Capacity <= 1)
        {
            return;
        }

        constexpr int maxWideSize = static_cast<int>(Capacity - 1);
        std::size_t inputSize = text.size();
        if (inputSize > Capacity - 1)
        {
            inputSize = Capacity - 1;
        }

        int wideSize = 0;
        if (inputSize > 0)
        {
            wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(inputSize), output.data(), maxWideSize);
        }

        if (wideSize > 0)
        {
            for (int index = 0; index < wideSize; ++index)
            {
                if (output[static_cast<std::size_t>(index)] == L'\0')
                {
                    output[static_cast<std::size_t>(index)] = L'?';
                }
            }
            output[static_cast<std::size_t>(wideSize)] = L'\0';
            return;
        }

        wideSize = static_cast<int>(inputSize);
        if (wideSize > maxWideSize)
        {
            wideSize = maxWideSize;
        }

        for (int index = 0; index < wideSize; ++index)
        {
            const unsigned char value = static_cast<unsigned char>(text[static_cast<std::size_t>(index)]);
            output[static_cast<std::size_t>(index)] = value >= 0x20 && value < 0x80 ? static_cast<wchar_t>(value) : L'?';
        }
        output[static_cast<std::size_t>(wideSize)] = L'\0';
    }

    int buttonIdForAction(FailureAction action) noexcept
    {
        switch (action)
        {
        case FailureAction::Break:
            return 1001;
        case FailureAction::Abort:
            return 1002;
        case FailureAction::IgnoreOnce:
            return 1003;
        case FailureAction::AlwaysIgnore:
            return 1004;
        }

        return 1002;
    }

    FailureAction actionForButtonId(int buttonId, FailureAction defaultAction) noexcept
    {
        switch (buttonId)
        {
        case 1001:
            return FailureAction::Break;
        case 1002:
            return FailureAction::Abort;
        case 1003:
            return FailureAction::IgnoreOnce;
        case 1004:
            return FailureAction::AlwaysIgnore;
        default:
            return defaultAction;
        }
    }

    using TaskDialogIndirectFn = HRESULT(WINAPI *)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

    TaskDialogIndirectFn loadTaskDialogIndirect() noexcept
    {
        HMODULE commonControls = GetModuleHandleW(L"comctl32.dll");
        if (commonControls == nullptr)
        {
            commonControls = LoadLibraryW(L"comctl32.dll");
        }

        if (commonControls == nullptr)
        {
            return nullptr;
        }

        return reinterpret_cast<TaskDialogIndirectFn>(GetProcAddress(commonControls, "TaskDialogIndirect"));
    }

    FailureAction fallbackMessageBoxAction(const wchar_t *title, const wchar_t *message, FailureAction defaultAction) noexcept
    {
        const int result = MessageBoxW(nullptr, message, title, MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
        switch (result)
        {
        case IDABORT:
            return FailureAction::Abort;
        case IDRETRY:
            return FailureAction::Break;
        case IDIGNORE:
            return FailureAction::IgnoreOnce;
        default:
            return defaultAction;
        }
    }
}

namespace GameWIP::Debug::Assert::Platform
{
    void showErrorPopup(std::string_view title, std::string_view message) noexcept
    {
        if (popupsSuppressed())
        {
            return;
        }

        std::array<wchar_t, 128> titleText;
        std::array<wchar_t, 1025> messageText;
        utf8ToWide(title, titleText);
        utf8ToWide(message, messageText);
        MessageBoxW(nullptr, messageText.data(), titleText.data(), MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
    }

    FailureAction showFailureActionDialog(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept
    {
        std::array<wchar_t, 128> titleText;
        std::array<wchar_t, 1025> messageText;
        utf8ToWide(title, titleText);
        utf8ToWide(message, messageText);

        constexpr int breakId = 1001;
        constexpr int abortId = 1002;
        constexpr int ignoreOnceId = 1003;
        constexpr int alwaysIgnoreId = 1004;

        const TASKDIALOG_BUTTON buttons[] =
            {
                {breakId, L"Break"},
                {abortId, L"Abort"},
                {ignoreOnceId, L"Ignore Once"},
                {alwaysIgnoreId, L"Always Ignore"},
            };

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = titleText.data();
        config.pszMainInstruction = titleText.data();
        config.pszContent = messageText.data();
        config.pszMainIcon = TD_ERROR_ICON;
        config.pButtons = buttons;
        config.cButtons = static_cast<UINT>(sizeof(buttons) / sizeof(buttons[0]));
        config.nDefaultButton = buttonIdForAction(defaultAction);

        int selectedButton = buttonIdForAction(defaultAction);
        if (const TaskDialogIndirectFn taskDialogIndirect = loadTaskDialogIndirect())
        {
            const HRESULT result = taskDialogIndirect(&config, &selectedButton, nullptr, nullptr);
            if (SUCCEEDED(result))
            {
                return actionForButtonId(selectedButton, defaultAction);
            }
        }

        return fallbackMessageBoxAction(titleText.data(), messageText.data(), defaultAction);
    }

    bool isDebuggerAttached() noexcept
    {
        return IsDebuggerPresent() != FALSE;
    }

    void debugBreak() noexcept
    {
        DebugBreak();
    }
}
