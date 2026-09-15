/// @file win32_assert.cpp
/// @brief Windows platform backend for Assert UI, debugger detection, and debug breaks.
/// @details The backend prefers Task Dialog for interactive failures, falls back to MessageBox,
/// converts UTF-8 diagnostics to UTF-16, and bounds popup text before showing UI.

#include "debug/assert/internal/assert_platform.h"
#include "debug/assert/internal/assert_test_hooks.h"
#include "base/platform/win32/dynamic_library.h"
#include "unicode/unicode.h"

#include <atomic>
#include <new>
#include <string>
#include <string_view>
#include <vector>

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
    // ------------------------------------------------------------
    // Popup suppression
    // ------------------------------------------------------------

    /// @brief Returns whether real Assert UI is suppressed by environment state.
    bool popupsSuppressed() noexcept
    {
        // Only the exact value "1" suppresses UI. Two code units hold that value and its terminator.
        wchar_t value[2]{};
        const DWORD size = GetEnvironmentVariableW(L"INTERNAL_ASSERT_SUPPRESS_POPUP", value, static_cast<DWORD>(sizeof(value) / sizeof(value[0])));
        return size == 1 && value[0] == L'1';
    }

    using FailureAction = GameWIP::Debug::Assert::FailureAction;

#if ASSERT_INTERNAL_TEST_HOOKS
    /// @brief One-shot diagnostic preparation failure used by the focused noexcept test.
    std::atomic_bool nextDiagnosticPreparationFailure = false;

    /// @brief Suppresses only the native presentation made by the dedicated test adapter.
    thread_local bool suppressNativePresentationForTest = false;
#endif

    void presentErrorPopup(const wchar_t *title, const wchar_t *message) noexcept
    {
#if ASSERT_INTERNAL_TEST_HOOKS
        if (suppressNativePresentationForTest)
        {
            return;
        }
#endif
        static_cast<void>(MessageBoxW(nullptr, message, title, MB_ICONERROR | MB_OK | MB_SETFOREGROUND));
    }

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
    std::wstring utf8ToWide(std::string_view text)
    {
#if ASSERT_INTERNAL_TEST_HOOKS
        if (nextDiagnosticPreparationFailure.exchange(false, std::memory_order_acq_rel))
        {
            throw std::bad_alloc{};
        }
#endif
        if (text.empty())
        {
            return {};
        }

        const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            return asciiFallbackToWide(text);
        }

        std::vector<char16_t> converted(measurement.requiredCodeUnits);
        const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            return asciiFallbackToWide(text);
        }

        std::wstring output(conversion.codeUnitsWritten, L'\0');
        for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
        {
            // Native dialogs stop at NUL; replacement keeps the following text visible.
            const char16_t codeUnit = converted[index];
            output[index] = codeUnit == u'\0' ? L'?' : static_cast<wchar_t>(codeUnit);
        }
        return output;
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
    PopupText preparePopupText(std::string_view title, std::string_view message)
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

        // Intentionally retain comctl32 for process lifetime: the resolved procedure pointer may
        // be reused and must remain valid after this function returns.
        return GameWIP::Base::Win32::loadProcedure<TaskDialogIndirectFn>(commonControls, "TaskDialogIndirect");
    }

    /// @brief Presents the reduced Abort/Break/Ignore fallback through MessageBoxW.
    /// @details MessageBoxW cannot represent the full Always Ignore action set.
    FailureAction fallbackMessageBoxAction(const wchar_t *title, const wchar_t *message, FailureAction defaultAction) noexcept
    {
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
    bool runDiagnosticPreparationEmergencyPathForTest() noexcept
    {
        nextDiagnosticPreparationFailure.store(true, std::memory_order_release);
        const bool previous = suppressNativePresentationForTest;
        suppressNativePresentationForTest = true;
        GameWIP::Debug::Assert::Detail::Platform::showErrorPopup(
            "Assert diagnostic preparation",
            "The static emergency popup path must remain noexcept.");
        suppressNativePresentationForTest = previous;

        const bool failureRemainedArmed = nextDiagnosticPreparationFailure.exchange(false, std::memory_order_acq_rel);
        return !failureRemainedArmed;
    }
} // namespace GameWIP::Debug::Assert::TestHooks
#endif


namespace GameWIP::Debug::Assert::Detail::Platform
{
    void showErrorPopup(std::string_view title, std::string_view message) noexcept
    {
        if (popupsSuppressed())
        {
            return;
        }
        try
        {
            const PopupText text = preparePopupText(title, message);
            presentErrorPopup(text.title.c_str(), text.message.c_str());
        }
        catch (...)
        {
            presentErrorPopup(L"GameWIP Assert", L"Assertion failure.");
        }
    }

    FailureAction showFailureActionDialog(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept
    {
        try
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
            if (const TaskDialogIndirectFn taskDialogIndirect = loadTaskDialogIndirect())
            {
                const HRESULT result = taskDialogIndirect(&config, &selectedButton, nullptr, nullptr);
                if (SUCCEEDED(result))
                {
                    return actionForButtonId(selectedButton, defaultAction);
                }
            }

            return fallbackMessageBoxAction(text.title.c_str(), text.message.c_str(), defaultAction);
        }
        catch (...)
        {
            // This emergency path must not allocate or convert: use static UTF-16 literals only.
            return fallbackMessageBoxAction(L"GameWIP Assert", L"Assertion failure.", defaultAction);
        }
    }

    bool isDebuggerAttached() noexcept
    {

        return IsDebuggerPresent() != FALSE;
    }

    void debugBreak() noexcept
    {
        DebugBreak();
    }
} // namespace GameWIP::Debug::Assert::Detail::Platform
