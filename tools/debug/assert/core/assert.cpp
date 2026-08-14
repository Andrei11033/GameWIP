/// @file assert.cpp
/// @brief Core runtime implementation for Assert failure handling.
/// @details This file owns bounded failure formatting, synchronous Logger reporting, popup dispatch,
/// debugger-break policy, abort policy, and interactive action application.

#include "debug/assert/assert.h"
#include "debug/assert/internal/assert_platform.h"

#ifndef ASSERT_INTERNAL_TEST_HOOKS
#define ASSERT_INTERNAL_TEST_HOOKS 0
#endif

#if ASSERT_INTERNAL_TEST_HOOKS
#include "debug/assert/internal/assert_test_hooks.h"
#endif

#include "logger/logger.h"

#if ASSERT_DIAGNOSTICS
#include <array>
#include <charconv>
#endif
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace
{
    /// @brief Failure kind used for message prefixes and logger sources.
    enum class FailureKind
    {
        Assert,
        Check
    };

    using FailureAction = GameWIP::Debug::Assert::FailureAction;

#if ASSERT_DIAGNOSTICS
    /// @brief Returns whether one byte is a UTF-8 continuation byte.
    constexpr bool isUtf8ContinuationByte(char value) noexcept
    {
        return (static_cast<unsigned char>(value) & 0xC0u) == 0x80u;
    }

    /// @brief Returns the largest prefix at or before maxBytes that does not split a UTF-8 scalar.
    /// @details Assert diagnostic inputs follow the public UTF-8 precondition; this helper only
    /// adjusts a bounded-copy cut point and intentionally does not rescan the whole fragment.
    std::size_t utf8PrefixBoundary(std::string_view text, std::size_t maxBytes) noexcept
    {
        if (maxBytes >= text.size())
        {
            return text.size();
        }

        std::size_t boundary = maxBytes;
        while (boundary > 0 && isUtf8ContinuationByte(text[boundary]))
        {
            --boundary;
        }
        return boundary;
    }

    /// @brief Fixed-size stack message builder used to keep failure formatting allocation-free.
    class FixedFailureMessage
    {
    public:
        /// @brief Appends UTF-8 text, truncating only at a scalar boundary when fixed storage fills.
        /// @param text UTF-8 text fragment to append.
        void append(std::string_view text) noexcept
        {
            if (truncated || text.empty())
            {
                return;
            }

            const std::size_t remaining = storage.size() - size;
            if (text.size() <= remaining)
            {
                std::memcpy(storage.data() + size, text.data(), text.size());
                size += text.size();
                return;
            }

            const std::size_t prefixBytes = utf8PrefixBoundary(text, remaining);
            if (prefixBytes > 0)
            {
                std::memcpy(storage.data() + size, text.data(), prefixBytes);
                size += prefixBytes;
            }
            truncated = true;
        }

        /// @brief Appends a signed integer without allocating.
        /// @param value Integer value to append.
        void appendInt(int value) noexcept
        {
            std::array<char, 32> number;
            const auto result = std::to_chars(number.data(), number.data() + number.size(), value);
            if (result.ec == std::errc{})
            {
                append(std::string_view(number.data(), static_cast<std::size_t>(result.ptr - number.data())));
                return;
            }
            append("?");
        }

        /// @brief Applies a visible ASCII truncation suffix without cutting the retained UTF-8 prefix.
        void finish() noexcept
        {
            if (!truncated)
            {
                return;
            }

            constexpr std::string_view suffix = "... [truncated]";
            static_assert(suffix.size() < 1024);
            const std::size_t prefixLimit = storage.size() - suffix.size();
            size = utf8PrefixBoundary(std::string_view(storage.data(), size), prefixLimit);
            std::memcpy(storage.data() + size, suffix.data(), suffix.size());
            size += suffix.size();
        }

        /// @brief Returns the active UTF-8 text view.
        /// @return Failure message text.
        std::string_view view() const noexcept
        {
            return std::string_view(storage.data(), size);
        }

    private:
        /// @brief Fixed stack storage for bounded diagnostics.
        std::array<char, 1024> storage{};
        /// @brief Active byte count in storage.
        std::size_t size = 0;
        /// @brief True when an append exceeded fixed capacity.
        bool truncated = false;
    };
#endif

    /// @brief Returns the logger source/debug label for one failure kind.
    /// @param kind Failure kind to label.
    /// @return Static label text.
    constexpr std::string_view sourceText(FailureKind kind) noexcept
    {
        return kind == FailureKind::Assert ? "Assert" : "Check";
    }

    /// @brief Returns the assert-owned popup title for one failure kind.
    /// @param kind Failure kind to label.
    /// @return Static popup title text.
    constexpr std::string_view popupTitle(FailureKind kind) noexcept
    {
        return kind == FailureKind::Assert ? "Assertion Failed" : "Check Failed";
    }

    /// @brief Returns whether an assert-owned popup is compiled on for one failure kind.
    /// @param kind Failure kind to inspect.
    /// @return True when this failure kind should show a popup.
    constexpr bool popupEnabled(FailureKind kind) noexcept
    {
        return kind == FailureKind::Assert ? (ASSERT_POPUP_ON_ASSERT != 0) : (ASSERT_POPUP_ON_CHECK != 0);
    }

#if ASSERT_DIAGNOSTICS
    /// @brief Builds the bounded diagnostic text for one failed assertion/check.
    /// @param kind Failure kind to include in the prefix.
    /// @param conditionText Expression text, or empty when diagnostics are disabled.
    /// @param message Caller message, or empty when absent/diagnostics are disabled.
    /// @param file Source file text, or empty when diagnostics are disabled.
    /// @param line Source line, or zero when diagnostics are disabled.
    /// @param function Function text, or empty when diagnostics are disabled.
    /// @return Fixed stack-backed failure message.
    FixedFailureMessage buildFailureMessage(
        FailureKind kind,
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept
    {
        FixedFailureMessage failureMessage;
        failureMessage.append(sourceText(kind));
        failureMessage.append(" failed");

        if (!conditionText.empty())
        {
            failureMessage.append(": ");
            failureMessage.append(conditionText);
        }

        if (!message.empty())
        {
            failureMessage.append("\nMessage: ");
            failureMessage.append(message);
        }

        if (!file.empty() || line > 0 || !function.empty())
        {
            failureMessage.append("\nLocation: ");
            if (!file.empty())
            {
                failureMessage.append(file);
            }
            if (line > 0)
            {
                failureMessage.append(":");
                failureMessage.appendInt(line);
            }
            if (!function.empty())
            {
                failureMessage.append(" (");
                failureMessage.append(function);
                failureMessage.append(")");
            }
        }

        failureMessage.finish();
        return failureMessage;
    }
#else
    /// @brief Returns the tiny generic failure text used when diagnostic payloads are compiled out.
    /// @param kind Failure kind to label.
    /// @return Static failure text.
    constexpr std::string_view buildFailureMessage(
        FailureKind kind,
        std::string_view,
        std::string_view,
        std::string_view,
        int,
        std::string_view) noexcept
    {
        return kind == FailureKind::Assert ? "Assert failed" : "Check failed";
    }
#endif

    /// @brief Reports one failure through the synchronous Logger report path.
    /// @param kind Failure kind used as the Logger source and severity selector.
    /// @param message Failure message text.
    void reportFailure(FailureKind kind, std::string_view message) noexcept
    {
        try
        {
            const GameWIP::Logger::Types::Level level =
                kind == FailureKind::Assert ? GameWIP::Logger::Types::Level::Fatal : GameWIP::Logger::Types::Level::Error;
            GameWIP::Logger::report(level, sourceText(kind), message);
        }
        catch (...) // NOLINT(bugprone-empty-catch) -- Failure reporting must preserve this noexcept boundary.
        {
        }
    }

    /// @brief Shows the assert-owned popup when enabled for this failure kind.
    /// @param kind Failure kind used for title/config selection.
    /// @param message Message text to show.
    void showPopupIfEnabled(FailureKind kind, std::string_view message) noexcept
    {
        if (!popupEnabled(kind))
        {
            return;
        }

        GameWIP::Debug::Assert::Detail::Platform::showErrorPopup(popupTitle(kind), message);
    }

    /// @brief Parses a test override action string for interactive asserts.
    /// @param text Environment value text.
    /// @param action Output action on success.
    /// @return True when text names a valid action.
    bool parseFailureAction(std::string_view text, FailureAction &action) noexcept
    {
        if (text == "break")
        {
            action = FailureAction::Break;
            return true;
        }
        if (text == "abort")
        {
            action = FailureAction::Abort;
            return true;
        }
        if (text == "ignore_once")
        {
            action = FailureAction::IgnoreOnce;
            return true;
        }
        if (text == "always_ignore")
        {
            action = FailureAction::AlwaysIgnore;
            return true;
        }

        return false;
    }

    /// @brief Returns true when real assert UI is suppressed by the validation override or environment.
    /// @return True when a hook forces suppression or `INTERNAL_ASSERT_SUPPRESS_POPUP` is exactly `1`.
    bool popupsSuppressedByEnvironment() noexcept
    {
#if ASSERT_INTERNAL_TEST_HOOKS
        bool overrideValue = false;
        if (GameWIP::Debug::Assert::Detail::TestHooks::popupSuppressedOverride(overrideValue))
        {
            return overrideValue;
        }
#endif
        const char *value = std::getenv("INTERNAL_ASSERT_SUPPRESS_POPUP");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }

    /// @brief Returns the safest default action for the current debugger state.
    /// @return Break when a debugger is attached, otherwise Abort.
    FailureAction defaultInteractiveAction() noexcept
    {
#if ASSERT_INTERNAL_TEST_HOOKS
        bool attachedOverride = false;
        if (GameWIP::Debug::Assert::Detail::TestHooks::debuggerAttachedOverride(attachedOverride))
        {
            return attachedOverride ? FailureAction::Break : FailureAction::Abort;
        }
#endif
        return GameWIP::Debug::Assert::Detail::Platform::isDebuggerAttached() ? FailureAction::Break : FailureAction::Abort;
    }

    /// @brief Selects an action for one interactive fatal assert failure.
    /// @param message Failure text to display in the platform action dialog.
    /// @return Selected action from test override, suppression/default policy, or platform UI.
    FailureAction selectInteractiveAction(std::string_view message) noexcept
    {
        if (const char *testActionText = std::getenv("INTERNAL_ASSERT_TEST_ACTION"))
        {
            FailureAction testAction = FailureAction::Abort;
            if (parseFailureAction(testActionText, testAction))
            {
                return testAction;
            }
        }

        if (popupsSuppressedByEnvironment())
        {
            return FailureAction::Abort;
        }

        const FailureAction defaultAction = defaultInteractiveAction();
        return GameWIP::Debug::Assert::Detail::Platform::showFailureActionDialog(popupTitle(FailureKind::Assert), message, defaultAction);
    }

    /// @brief Applies the selected action for an interactive fatal assert failure.
    /// @param action Action selected by test override, popup suppression, or platform UI.
    /// @param alwaysIgnoreFlag Per-call-site flag to set for Always Ignore. Null means no site can be suppressed.
    void applyInteractiveAction(FailureAction action, std::atomic_bool *alwaysIgnoreFlag) noexcept
    {
        switch (action)
        {
        case FailureAction::Break:
            GameWIP::Debug::Assert::Detail::Platform::debugBreak();
            return;

        case FailureAction::Abort:
            std::abort();

        case FailureAction::IgnoreOnce:
            return;

        case FailureAction::AlwaysIgnore:
            if (alwaysIgnoreFlag != nullptr)
            {
                alwaysIgnoreFlag->store(true, std::memory_order_relaxed);
            }
            return;
        }

        std::abort();
    }

    /// @brief Returns the active text view for a failure message object or generic string view.
    /// @param message Failure message object.
    /// @return Message text.
#if ASSERT_DIAGNOSTICS
    std::string_view failureTextView(const FixedFailureMessage &message) noexcept
    {
        return message.view();
    }
#else
    std::string_view failureTextView(std::string_view message) noexcept
    {
        return message;
    }
#endif

    /// @brief Reports one failed non-interactive assertion through Logger and the Assert-owned popup path.
    /// @param conditionText Expression text, or empty when diagnostics are disabled.
    /// @param message Caller message, or empty when absent/diagnostics are disabled.
    /// @param file Source file text, or empty when diagnostics are disabled.
    /// @param line Source line, or zero when diagnostics are disabled.
    /// @param function Function text, or empty when diagnostics are disabled.
    void reportAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept
    {
        const auto failureMessage = buildFailureMessage(FailureKind::Assert, conditionText, message, file, line, function);
        const std::string_view failureText = failureTextView(failureMessage);
        reportFailure(FailureKind::Assert, failureText);
        showPopupIfEnabled(FailureKind::Assert, failureText);
    }

    /// @brief Reports one failed recoverable check through Logger and optional assert-owned popup path.
    /// @param conditionText Expression text, or empty when diagnostics are disabled.
    /// @param message Caller message, or empty when absent/diagnostics are disabled.
    /// @param file Source file text, or empty when diagnostics are disabled.
    /// @param line Source line, or zero when diagnostics are disabled.
    /// @param function Function text, or empty when diagnostics are disabled.
    void reportCheckFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept
    {
        const auto failureMessage = buildFailureMessage(FailureKind::Check, conditionText, message, file, line, function);
        const std::string_view failureText = failureTextView(failureMessage);
        reportFailure(FailureKind::Check, failureText);
        showPopupIfEnabled(FailureKind::Check, failureText);
    }

    /// @brief Reports one failed interactive assertion and applies the selected action.
    /// @param conditionText Expression text, or empty when diagnostics are disabled.
    /// @param message Caller message, or empty when absent/diagnostics are disabled.
    /// @param file Source file text, or empty when diagnostics are disabled.
    /// @param line Source line, or zero when diagnostics are disabled.
    /// @param function Function text, or empty when diagnostics are disabled.
    /// @param alwaysIgnoreFlag Per-call-site suppression flag for Always Ignore.
    void reportInteractiveAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function,
        std::atomic_bool *alwaysIgnoreFlag) noexcept
    {
        const auto failureMessage = buildFailureMessage(FailureKind::Assert, conditionText, message, file, line, function);
        const std::string_view failureText = failureTextView(failureMessage);
        reportFailure(FailureKind::Assert, failureText);

        const FailureAction action = selectInteractiveAction(failureText);
        applyInteractiveAction(action, alwaysIgnoreFlag);
    }
} // namespace

namespace GameWIP::Debug::Assert
{
    void debugBreak() noexcept
    {
        Detail::Platform::debugBreak();
    }
} // namespace GameWIP::Debug::Assert

namespace GameWIP::Debug::Assert::Detail
{
    [[noreturn]] void handleAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept
    {
        reportAssertFailure(conditionText, message, file, line, function);
#if ASSERT_INTERNAL_TEST_HOOKS
        bool attachedOverride = false;
        const bool debuggerAttached =
            TestHooks::debuggerAttachedOverride(attachedOverride) ? attachedOverride : Detail::Platform::isDebuggerAttached();
#else
        const bool debuggerAttached = Detail::Platform::isDebuggerAttached();
#endif
        if (debuggerAttached)
        {
            Detail::Platform::debugBreak();
        }
        std::abort();
    }

    void handleInteractiveAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function,
        std::atomic_bool *alwaysIgnoreFlag) noexcept
    {
        reportInteractiveAssertFailure(conditionText, message, file, line, function, alwaysIgnoreFlag);
    }

    void handleCheckFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept
    {
        reportCheckFailure(conditionText, message, file, line, function);
    }
} // namespace GameWIP::Debug::Assert::Detail
