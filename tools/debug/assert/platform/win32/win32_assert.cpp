/// @file win32_assert.cpp
/// @brief Windows platform backend for the GameWIP Assert library.

#include "debug/assert/internal/assert_platform.h"

#ifndef GAMEWIP_ASSERT_TEST_HOOKS
#define GAMEWIP_ASSERT_TEST_HOOKS 0
#endif

#if GAMEWIP_ASSERT_TEST_HOOKS
#include "debug/assert/internal/assert_test_hooks.h"
#endif

#include <limits>
#include <string>
#include <string_view>

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

#include <atomic>

namespace
{
    bool popupsSuppressed() noexcept
    {
#if GAMEWIP_ASSERT_TEST_HOOKS
        bool overrideValue = false;
        if (GameWIP::Debug::Assert::Detail::TestHooks::popupSuppressedOverride(overrideValue))
        {
            return overrideValue;
        }
#endif
        wchar_t value[2]{};
        const DWORD size = GetEnvironmentVariableW(L"GAMEWIP_ASSERT_SUPPRESS_POPUP", value, static_cast<DWORD>(sizeof(value) / sizeof(value[0])));
        return size == 1 && value[0] == L'1';
    }

    using FailureAction = GameWIP::Debug::Assert::FailureAction;

#if GAMEWIP_ASSERT_TEST_HOOKS
    struct AssertTestHookState
    {
        std::atomic_bool nextActionDialogFailure{false};
        std::atomic_bool nextFallbackActionDialogFailure{false};
        std::atomic_bool debuggerAttachedOverrideEnabled{false};
        std::atomic_bool debuggerAttachedOverrideValue{false};
        std::atomic_bool popupSuppressedOverrideEnabled{false};
        std::atomic_bool popupSuppressedOverrideValue{false};
    };

    AssertTestHookState assertTestHookState;

    bool consumeTestHook(std::atomic_bool &flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }
#endif

    /// @brief Builds a printable ASCII-only wide string when strict UTF-8 conversion fails.
    std::wstring asciiFallbackToWide(std::string_view text)
    {
        std::wstring output;
        output.reserve(text.size());
        for (char ch : text)
        {
            const unsigned char value = static_cast<unsigned char>(ch);
            output.push_back(value >= 0x20 && value < 0x80 ? static_cast<wchar_t>(value) : L'?');
        }
        return output;
    }

    /// @brief Converts full UTF-8 text to UTF-16 before popup truncation.
    /// @details Truncating after conversion avoids splitting a multibyte UTF-8 sequence.
    std::wstring utf8ToWide(std::string_view text) noexcept
    {
        try
        {
            if (text.empty())
            {
                return {};
            }

            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return asciiFallbackToWide(text);
            }

            const int inputSize = static_cast<int>(text.size());
            const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
            if (wideSize <= 0)
            {
                return asciiFallbackToWide(text);
            }

            std::wstring output(static_cast<std::size_t>(wideSize), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, output.data(), wideSize) != wideSize)
            {
                return asciiFallbackToWide(text);
            }

            for (wchar_t &value : output)
            {
                if (value == L'\0')
                {
                    value = L'?';
                }
            }

            return output;
        }
        catch (...)
        {
            return L"?";
        }
    }

    /// @brief Bounds popup text after UTF-16 conversion while preserving a visible suffix.
    void truncateWideForPopup(std::wstring &text, std::size_t maxCharacters) noexcept
    {
        if (text.size() <= maxCharacters)
        {
            return;
        }

        constexpr std::wstring_view suffix = L"... [truncated]";
        if (maxCharacters <= suffix.size())
        {
            text.resize(maxCharacters);
            return;
        }

        text.resize(maxCharacters);
        const std::size_t suffixOffset = maxCharacters - suffix.size();
        for (std::size_t index = 0; index < suffix.size(); ++index)
        {
            text[suffixOffset + index] = suffix[index];
        }
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
#if GAMEWIP_ASSERT_TEST_HOOKS
        if (consumeTestHook(assertTestHookState.nextFallbackActionDialogFailure))
        {
            return defaultAction;
        }
#endif
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


#if GAMEWIP_ASSERT_TEST_HOOKS
namespace GameWIP::Debug::Assert::TestHooks
{
    void reset() noexcept
    {
        assertTestHookState.nextActionDialogFailure.store(false, std::memory_order_release);
        assertTestHookState.nextFallbackActionDialogFailure.store(false, std::memory_order_release);
        assertTestHookState.debuggerAttachedOverrideEnabled.store(false, std::memory_order_release);
        assertTestHookState.debuggerAttachedOverrideValue.store(false, std::memory_order_release);
        assertTestHookState.popupSuppressedOverrideEnabled.store(false, std::memory_order_release);
        assertTestHookState.popupSuppressedOverrideValue.store(false, std::memory_order_release);
    }

    void forceNextActionDialogFailure() noexcept
    {
        assertTestHookState.nextActionDialogFailure.store(true, std::memory_order_release);
    }

    void forceNextFallbackActionDialogFailure() noexcept
    {
        assertTestHookState.nextFallbackActionDialogFailure.store(true, std::memory_order_release);
    }

    void setDebuggerAttachedOverride(bool attached) noexcept
    {
        assertTestHookState.debuggerAttachedOverrideValue.store(attached, std::memory_order_release);
        assertTestHookState.debuggerAttachedOverrideEnabled.store(true, std::memory_order_release);
    }

    void clearDebuggerAttachedOverride() noexcept
    {
        assertTestHookState.debuggerAttachedOverrideEnabled.store(false, std::memory_order_release);
    }

    void setPopupSuppressedOverride(bool suppressed) noexcept
    {
        assertTestHookState.popupSuppressedOverrideValue.store(suppressed, std::memory_order_release);
        assertTestHookState.popupSuppressedOverrideEnabled.store(true, std::memory_order_release);
    }

    void clearPopupSuppressedOverride() noexcept
    {
        assertTestHookState.popupSuppressedOverrideEnabled.store(false, std::memory_order_release);
    }

    bool debuggerAttachedForTest() noexcept
    {
        return GameWIP::Debug::Assert::Detail::Platform::isDebuggerAttached();
    }

    FailureAction showFailureActionDialogForTest(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept
    {
        return GameWIP::Debug::Assert::Detail::Platform::showFailureActionDialog(title, message, defaultAction);
    }

    void showErrorPopupForTest(std::string_view title, std::string_view message) noexcept
    {
        GameWIP::Debug::Assert::Detail::Platform::showErrorPopup(title, message);
    }

} // namespace GameWIP::Debug::Assert::TestHooks

namespace GameWIP::Debug::Assert::Detail::TestHooks
{
        bool consumeNextActionDialogFailure() noexcept
        {
            return consumeTestHook(assertTestHookState.nextActionDialogFailure);
        }

        bool consumeNextFallbackActionDialogFailure() noexcept
        {
            return consumeTestHook(assertTestHookState.nextFallbackActionDialogFailure);
        }

        bool debuggerAttachedOverride(bool &attached) noexcept
        {
            if (!assertTestHookState.debuggerAttachedOverrideEnabled.load(std::memory_order_acquire))
            {
                return false;
            }
            attached = assertTestHookState.debuggerAttachedOverrideValue.load(std::memory_order_acquire);
            return true;
        }

        bool popupSuppressedOverride(bool &suppressed) noexcept
        {
            if (!assertTestHookState.popupSuppressedOverrideEnabled.load(std::memory_order_acquire))
            {
                return false;
            }
            suppressed = assertTestHookState.popupSuppressedOverrideValue.load(std::memory_order_acquire);
            return true;
        }
} // namespace GameWIP::Debug::Assert::Detail::TestHooks
#endif

namespace GameWIP::Debug::Assert::Detail::Platform
{
    void showErrorPopup(std::string_view title, std::string_view message) noexcept
    {
        if (popupsSuppressed())
        {
            return;
        }

        std::wstring titleText = utf8ToWide(title);
        std::wstring messageText = utf8ToWide(message);
        truncateWideForPopup(titleText, 127);
        truncateWideForPopup(messageText, 1024);
        MessageBoxW(nullptr, messageText.c_str(), titleText.c_str(), MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
    }

    FailureAction showFailureActionDialog(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept
    {
        std::wstring titleText = utf8ToWide(title);
        std::wstring messageText = utf8ToWide(message);
        truncateWideForPopup(titleText, 127);
        truncateWideForPopup(messageText, 1024);

        constexpr int kBreakButtonId = 1001;
        constexpr int kAbortButtonId = 1002;
        constexpr int kIgnoreOnceButtonId = 1003;
        constexpr int kAlwaysIgnoreButtonId = 1004;

        const TASKDIALOG_BUTTON buttons[] =
            {
                {kBreakButtonId, L"Break"},
                {kAbortButtonId, L"Abort"},
                {kIgnoreOnceButtonId, L"Ignore Once"},
                {kAlwaysIgnoreButtonId, L"Always Ignore"},
            };

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = titleText.c_str();
        config.pszMainInstruction = titleText.c_str();
        config.pszContent = messageText.c_str();
        config.pszMainIcon = TD_ERROR_ICON;
        config.pButtons = buttons;
        config.cButtons = static_cast<UINT>(sizeof(buttons) / sizeof(buttons[0]));
        config.nDefaultButton = buttonIdForAction(defaultAction);

        int selectedButton = buttonIdForAction(defaultAction);
#if GAMEWIP_ASSERT_TEST_HOOKS
        const bool forceTaskDialogFailure = consumeTestHook(assertTestHookState.nextActionDialogFailure);
#else
        const bool forceTaskDialogFailure = false;
#endif
        if (!forceTaskDialogFailure)
        {
            if (const TaskDialogIndirectFn taskDialogIndirect = loadTaskDialogIndirect())
            {
                const HRESULT result = taskDialogIndirect(&config, &selectedButton, nullptr, nullptr);
                if (SUCCEEDED(result))
                {
                    return actionForButtonId(selectedButton, defaultAction);
                }
            }
        }

        return fallbackMessageBoxAction(titleText.c_str(), messageText.c_str(), defaultAction);
    }

    bool isDebuggerAttached() noexcept
    {
#if GAMEWIP_ASSERT_TEST_HOOKS
        bool overrideValue = false;
        if (GameWIP::Debug::Assert::Detail::TestHooks::debuggerAttachedOverride(overrideValue))
        {
            return overrideValue;
        }
#endif
        return IsDebuggerPresent() != FALSE;
    }

    void debugBreak() noexcept
    {
        DebugBreak();
    }
}
