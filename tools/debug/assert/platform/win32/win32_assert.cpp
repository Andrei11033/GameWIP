#include "debug/assert/internal/assert_platform.h"

#ifndef GAMEWIP_ASSERT_TEST_HOOKS
#define GAMEWIP_ASSERT_TEST_HOOKS 0
#endif

#if GAMEWIP_ASSERT_TEST_HOOKS
#include "debug/assert/internal/assert_test_hooks.h"
#endif

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

#include <atomic>

namespace
{
    bool popupsSuppressed() noexcept
    {
#if GAMEWIP_ASSERT_TEST_HOOKS
        bool overrideValue = false;
        if (GameWIP::Debug::Assert::TestHooks::Detail::popupSuppressedOverride(overrideValue))
        {
            return overrideValue;
        }
#endif
        char value[2]{};
        const DWORD size = GetEnvironmentVariableA("GAMEWIP_ASSERT_SUPPRESS_POPUP", value, static_cast<DWORD>(sizeof(value)));
        return size == 1 && value[0] == '1';
    }

    using FailureAction = GameWIP::Debug::Assert::FailureAction;

#if GAMEWIP_ASSERT_TEST_HOOKS
    struct AssertTestHookState
    {
        std::atomic_bool nextTaskDialogFailure{false};
        std::atomic_bool nextMessageBoxFailure{false};
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
#if GAMEWIP_ASSERT_TEST_HOOKS
        if (consumeTestHook(assertTestHookState.nextMessageBoxFailure))
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
        assertTestHookState.nextTaskDialogFailure.store(false, std::memory_order_release);
        assertTestHookState.nextMessageBoxFailure.store(false, std::memory_order_release);
        assertTestHookState.debuggerAttachedOverrideEnabled.store(false, std::memory_order_release);
        assertTestHookState.debuggerAttachedOverrideValue.store(false, std::memory_order_release);
        assertTestHookState.popupSuppressedOverrideEnabled.store(false, std::memory_order_release);
        assertTestHookState.popupSuppressedOverrideValue.store(false, std::memory_order_release);
    }

    void forceNextTaskDialogFailure() noexcept
    {
        assertTestHookState.nextTaskDialogFailure.store(true, std::memory_order_release);
    }

    void forceNextMessageBoxFailure() noexcept
    {
        assertTestHookState.nextMessageBoxFailure.store(true, std::memory_order_release);
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
        return GameWIP::Debug::Assert::Platform::isDebuggerAttached();
    }

    FailureAction showFailureActionDialogForTest(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept
    {
        return GameWIP::Debug::Assert::Platform::showFailureActionDialog(title, message, defaultAction);
    }

    void showErrorPopupForTest(std::string_view title, std::string_view message) noexcept
    {
        GameWIP::Debug::Assert::Platform::showErrorPopup(title, message);
    }

    namespace Detail
    {
        bool consumeNextTaskDialogFailure() noexcept
        {
            return consumeTestHook(assertTestHookState.nextTaskDialogFailure);
        }

        bool consumeNextMessageBoxFailure() noexcept
        {
            return consumeTestHook(assertTestHookState.nextMessageBoxFailure);
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
    }
}
#endif

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
        config.pszWindowTitle = titleText.data();
        config.pszMainInstruction = titleText.data();
        config.pszContent = messageText.data();
        config.pszMainIcon = TD_ERROR_ICON;
        config.pButtons = buttons;
        config.cButtons = static_cast<UINT>(sizeof(buttons) / sizeof(buttons[0]));
        config.nDefaultButton = buttonIdForAction(defaultAction);

        int selectedButton = buttonIdForAction(defaultAction);
#if GAMEWIP_ASSERT_TEST_HOOKS
        const bool forceTaskDialogFailure = consumeTestHook(assertTestHookState.nextTaskDialogFailure);
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

        return fallbackMessageBoxAction(titleText.data(), messageText.data(), defaultAction);
    }

    bool isDebuggerAttached() noexcept
    {
#if GAMEWIP_ASSERT_TEST_HOOKS
        bool overrideValue = false;
        if (TestHooks::Detail::debuggerAttachedOverride(overrideValue))
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
