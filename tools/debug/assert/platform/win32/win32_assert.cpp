/// @file win32_assert.cpp
/// @brief Windows platform backend for Assert UI, debugger detection, and debug breaks.
/// @details The backend prefers Task Dialog for interactive failures, falls back to MessageBox,
/// converts UTF-8 diagnostics to UTF-16, and bounds popup text before showing UI.

#include "debug/assert/internal/assert_platform.h"
#include "base/platform/win32/dynamic_library.h"
#include "unicode/unicode.h"

#ifndef ASSERT_INTERNAL_TEST_HOOKS
#define ASSERT_INTERNAL_TEST_HOOKS 0
#endif

#if ASSERT_INTERNAL_TEST_HOOKS
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
    // ------------------------------------------------------------
    // Popup suppression and test state
    // ------------------------------------------------------------

    /// @brief Returns whether real Assert UI is suppressed by test override or environment state.
    bool popupsSuppressed() noexcept
    {
#if ASSERT_INTERNAL_TEST_HOOKS
        bool overrideValue = false;
        if (GameWIP::Debug::Assert::Detail::TestHooks::popupSuppressedOverride(overrideValue))
        {
            return overrideValue;
        }
#endif
        // Only the exact value "1" suppresses UI. Two code units hold that value and its terminator.
        wchar_t value[2]{};
        const DWORD size = GetEnvironmentVariableW(L"INTERNAL_ASSERT_SUPPRESS_POPUP", value, static_cast<DWORD>(sizeof(value) / sizeof(value[0])));
        return size == 1 && value[0] == L'1';
    }

    using FailureAction = GameWIP::Debug::Assert::FailureAction;

#if ASSERT_INTERNAL_TEST_HOOKS
    /// @brief Process-wide one-shot failures and persistent Assert backend overrides.
    struct AssertTestHookState
    {
        std::atomic_bool nextActionDialogFailure{false};
        std::atomic_bool nextFallbackActionDialogFailure{false};
        // Separate enable/value fields distinguish a forced false result from no override.
        // Tests serialize changes; the atomics do not make reset a single transaction.
        std::atomic_bool debuggerAttachedOverrideEnabled{false};
        std::atomic_bool debuggerAttachedOverrideValue{false};
        std::atomic_bool popupSuppressedOverrideEnabled{false};
        std::atomic_bool popupSuppressedOverrideValue{false};
    };

    /// @brief Shared Win32 hook state consumed by public test adapters and backend helpers.
    AssertTestHookState assertTestHookState;

    /// @brief Atomically consumes one one-shot backend failure flag.
    bool consumeTestHook(std::atomic_bool &flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }
#endif

    // ------------------------------------------------------------
    // Popup text conversion
    // ------------------------------------------------------------

    /// @brief Builds printable ASCII-only UTF-16 text when strict UTF-8 conversion fails.
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

            // Oversized input keeps the fallback behavior from the older native conversion path.
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return asciiFallbackToWide(text);
            }

            // Measurement validates the full input and reserves the exact native string capacity.
            const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
            if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
            {
                return asciiFallbackToWide(text);
            }

            // Unicode owns decoding and surrogate encoding. Copying scalar-sized results into
            // wchar_t storage avoids aliasing char16_t memory or allocating an intermediate string.
            std::wstring output;
            output.reserve(measurement.requiredCodeUnits);
            while (!text.empty())
            {
                const auto decoded = GameWIP::Unicode::Utf8::decodeScalar(text);
                const auto encoded = GameWIP::Unicode::Utf16::encodeScalar(decoded.scalar);
                for (std::size_t index = 0; index < encoded.codeUnitCount; ++index)
                {
                    // Native dialogs stop at NUL; replacement keeps the following text visible.
                    const char16_t codeUnit = encoded.codeUnits[index];
                    output.push_back(codeUnit == u'\0' ? L'?' : static_cast<wchar_t>(codeUnit));
                }

                // Successful measurement guarantees complete scalars throughout the borrowed input.
                text.remove_prefix(decoded.bytesConsumed);
            }

            return output;
        }
        catch (...)
        {
            return L"?";
        }
    }

    /// @brief Returns whether one UTF-16 code unit is a high surrogate.
    constexpr bool isHighSurrogate(wchar_t value) noexcept
    {
        return value >= static_cast<wchar_t>(0xD800) && value <= static_cast<wchar_t>(0xDBFF);
    }

    /// @brief Returns whether one UTF-16 code unit is a low surrogate.
    constexpr bool isLowSurrogate(wchar_t value) noexcept
    {
        return value >= static_cast<wchar_t>(0xDC00) && value <= static_cast<wchar_t>(0xDFFF);
    }

    /// @brief Returns a UTF-16 prefix boundary that never separates a surrogate pair.
    std::size_t utf16PrefixBoundary(std::wstring_view text, std::size_t maxCodeUnits) noexcept
    {
        if (maxCodeUnits >= text.size())
        {
            return text.size();
        }

        if (maxCodeUnits > 0 && isHighSurrogate(text[maxCodeUnits - 1]) && isLowSurrogate(text[maxCodeUnits]))
        {
            return maxCodeUnits - 1;
        }

        return maxCodeUnits;
    }

    /// @brief Bounds popup text after strict UTF-8 conversion without splitting a UTF-16 scalar.
    void truncateWideForPopup(std::wstring &text, std::size_t maxCodeUnits) noexcept
    {
        if (text.size() <= maxCodeUnits)
        {
            return;
        }

        constexpr std::wstring_view suffix = L"... [truncated]";
        if (maxCodeUnits <= suffix.size())
        {
            text.resize(utf16PrefixBoundary(text, maxCodeUnits));
            return;
        }

        // The suffix overwrites discarded text in existing storage, so truncation needs no allocation.
        const std::size_t prefixLimit = maxCodeUnits - suffix.size();
        const std::size_t prefixSize = utf16PrefixBoundary(text, prefixLimit);
        for (std::size_t index = 0; index < suffix.size(); ++index)
        {
            text[prefixSize + index] = suffix[index];
        }

        text.resize(prefixSize + suffix.size());
    }

    // Limits count UTF-16 code units in the displayed text, excluding the string terminator.
    constexpr std::size_t kPopupTitleMaxCodeUnits = 127;
    constexpr std::size_t kPopupMessageMaxCodeUnits = 1024;

    /// @brief Owns the converted title and message throughout a synchronous native dialog call.
    struct PopupText
    {
        std::wstring title;
        std::wstring message;
    };

    /// @brief Converts and bounds both popup fields without separating Unicode scalars.
    PopupText preparePopupText(std::string_view title, std::string_view message) noexcept
    {
        PopupText text{utf8ToWide(title), utf8ToWide(message)};
        truncateWideForPopup(text.title, kPopupTitleMaxCodeUnits);
        truncateWideForPopup(text.message, kPopupMessageMaxCodeUnits);

        return text;
    }

    // ------------------------------------------------------------
    // Dialog actions and fallback
    // ------------------------------------------------------------

    // Button construction and both mappings share these IDs to keep choices and results consistent.
    constexpr int kBreakButtonId = 1001;
    constexpr int kAbortButtonId = 1002;
    constexpr int kIgnoreOnceButtonId = 1003;
    constexpr int kAlwaysIgnoreButtonId = 1004;

    /// @brief Maps an Assert failure action to a stable TaskDialog custom-button id.
    int buttonIdForAction(FailureAction action) noexcept
    {
        switch (action)
        {
        case FailureAction::Break:
            return kBreakButtonId;
        case FailureAction::Abort:
            return kAbortButtonId;
        case FailureAction::IgnoreOnce:
            return kIgnoreOnceButtonId;
        case FailureAction::AlwaysIgnore:
            return kAlwaysIgnoreButtonId;
        }

        return kAbortButtonId;
    }

    /// @brief Maps TaskDialog output back to an Assert action with a safe fallback.
    FailureAction actionForButtonId(int buttonId, FailureAction defaultAction) noexcept
    {
        switch (buttonId)
        {
        case kBreakButtonId:
            return FailureAction::Break;
        case kAbortButtonId:
            return FailureAction::Abort;
        case kIgnoreOnceButtonId:
            return FailureAction::IgnoreOnce;
        case kAlwaysIgnoreButtonId:
            return FailureAction::AlwaysIgnore;
        default:
            return defaultAction;
        }
    }

    /// @brief Runtime-loaded TaskDialogIndirect signature for systems without static availability.
    using TaskDialogIndirectFn = HRESULT(WINAPI *)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

    /// @brief Resolves TaskDialogIndirect from Comctl32 without making it a static load-time dependency.
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

        // The module remains loaded while the returned procedure pointer can be used.
        return GameWIP::Base::Win32::loadProcedure<TaskDialogIndirectFn>(commonControls, "TaskDialogIndirect");
    }

    /// @brief Presents the reduced Abort/Break/Ignore fallback through MessageBoxW.
    /// @details MessageBoxW cannot represent the full Always Ignore action set.
    FailureAction fallbackMessageBoxAction(const wchar_t *title, const wchar_t *message, FailureAction defaultAction) noexcept
    {
#if ASSERT_INTERNAL_TEST_HOOKS
        if (consumeTestHook(assertTestHookState.nextFallbackActionDialogFailure))
        {
            return defaultAction;
        }
#endif
        // MessageBox uses fixed labels: Retry maps to Break, and Ignore affects this failure only.
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
} // namespace

#if ASSERT_INTERNAL_TEST_HOOKS
namespace GameWIP::Debug::Assert::TestHooks
{
    void reset() noexcept
    {
        assertTestHookState.nextActionDialogFailure.store(false, std::memory_order_release);
        assertTestHookState.nextFallbackActionDialogFailure.store(false, std::memory_order_release);

        // Disabled overrides restore platform/environment queries; stored values return to defaults.
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
        // The replacement value is published before readers can observe the enabled override.
        assertTestHookState.debuggerAttachedOverrideValue.store(attached, std::memory_order_release);
        assertTestHookState.debuggerAttachedOverrideEnabled.store(true, std::memory_order_release);
    }

    void clearDebuggerAttachedOverride() noexcept
    {
        assertTestHookState.debuggerAttachedOverrideEnabled.store(false, std::memory_order_release);
    }

    void setPopupSuppressedOverride(bool suppressed) noexcept
    {
        // The replacement value is published before readers can observe the enabled override.
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

        const PopupText text = preparePopupText(title, message);

        // This informational popup has no action selection; the caller retains failure policy.
        MessageBoxW(nullptr, text.message.c_str(), text.title.c_str(), MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
    }

    FailureAction showFailureActionDialog(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept
    {
        const PopupText text = preparePopupText(title, message);

        // Task Dialog exposes all four actions, including suppression at the originating call site.
        const TASKDIALOG_BUTTON buttons[] = {
            {kBreakButtonId, L"Break"},
            {kAbortButtonId, L"Abort"},
            {kIgnoreOnceButtonId, L"Ignore Once"},
            {kAlwaysIgnoreButtonId, L"Always Ignore"},
        };

        // Pointers borrow the local strings and button array through the synchronous dialog call.
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = text.title.c_str();
        config.pszMainInstruction = text.title.c_str();
        config.pszContent = text.message.c_str();
        config.pszMainIcon = TD_ERROR_ICON;
        config.pButtons = buttons;
        config.cButtons = static_cast<UINT>(sizeof(buttons) / sizeof(buttons[0]));
        config.nDefaultButton = buttonIdForAction(defaultAction);

        int selectedButton = buttonIdForAction(defaultAction);
#if ASSERT_INTERNAL_TEST_HOOKS
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

        return fallbackMessageBoxAction(text.title.c_str(), text.message.c_str(), defaultAction);
    }

    bool isDebuggerAttached() noexcept
    {
#if ASSERT_INTERNAL_TEST_HOOKS
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
} // namespace GameWIP::Debug::Assert::Detail::Platform
