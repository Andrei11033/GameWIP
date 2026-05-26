#pragma once

#include <atomic>
#include <string_view>

/// @file assert.h
/// @brief Assertion, verification, recoverable-check, unreachable, and debug-break macros.
///
/// Public contract:
/// - ASSERT / ASSERT_MSG are fatal debug assertions. When enabled, a false condition
///   reports synchronously at Logger Fatal severity, may show the assert-owned popup,
///   breaks only when a debugger is attached, then aborts. When disabled, the condition
///   is not evaluated.
/// - ASSERT_INTERACTIVE / ASSERT_INTERACTIVE_MSG are separate fatal developer assertions.
///   On failure they report synchronously at Logger Fatal severity, then ask for a
///   FailureAction: Break, Abort, Ignore Once, or Always Ignore. They may continue.
/// - VERIFY / VERIFY_MSG preserve expression side effects. They always evaluate the
///   expression and only report/abort in assertion-enabled builds.
/// - VERIFY_INTERACTIVE / VERIFY_INTERACTIVE_MSG always evaluate the expression once.
///   In assertion-enabled builds, false results enter the same interactive fatal path.
/// - Interactive Always Ignore is per macro call site, not global. The normal ASSERT
///   and VERIFY behavior is unchanged by the interactive macros.
/// - GAMEWIP_ASSERT_TEST_ACTION can force interactive test actions: break, abort,
///   ignore_once, or always_ignore. GAMEWIP_ASSERT_SUPPRESS_POPUP=1 makes interactive
///   failures choose Abort unless GAMEWIP_ASSERT_TEST_ACTION provides a valid action.
/// - CHECK / CHECK_MSG are recoverable diagnostics. When enabled, a false condition
///   reports synchronously at Logger Error severity and execution continues. When
///   disabled, the condition is not evaluated.
/// - CHECK_ONCE / CHECK_ONCE_MSG report only the first failure attempt per call site.
/// - ENSURE / ENSURE_MSG always evaluate once and return the boolean result; false
///   results report only when recoverable checks are enabled.
/// - UNREACHABLE marks impossible control flow. Enabled builds use the fatal assert
///   path; disabled builds use the configured trap/unreachable hint path.
/// - DEBUG_BREAK explicitly enters the debugger/trap path regardless of assert/check settings.
///
/// Diagnostics are controlled by GAMEWIP_ASSERT_DIAGNOSTICS. When diagnostics are off,
/// condition text, file, line, function, and custom messages are intentionally stripped.

/// @def GAMEWIP_ASSERT_RUNTIME
/// @brief Internal/exported build flag that tells the header whether the assert runtime library is available.
/// @details 1 means runtime handlers are linked; 0 means the header uses inline trap/no-op behavior only.
#ifndef GAMEWIP_ASSERT_RUNTIME
#define GAMEWIP_ASSERT_RUNTIME 1
#endif

/// @def GAMEWIP_ASSERT_API
/// @brief DLL import/export marker used by the assert runtime declarations.
/// @details This is not part of normal user code; use the ASSERT/CHECK/ENSURE macro API instead.
#if defined(_WIN32)
#if defined(GAMEWIP_ASSERT_BUILD)
#define GAMEWIP_ASSERT_API __declspec(dllexport)
#elif GAMEWIP_ASSERT_RUNTIME
#define GAMEWIP_ASSERT_API __declspec(dllimport)
#else
#define GAMEWIP_ASSERT_API
#endif
#else
#define GAMEWIP_ASSERT_API
#endif

// Public convenience macros are intentionally global:
// ASSERT, ASSERT_MSG, ASSERT_INTERACTIVE, ASSERT_INTERACTIVE_MSG, VERIFY,
// VERIFY_MSG, VERIFY_INTERACTIVE, VERIFY_INTERACTIVE_MSG, CHECK, CHECK_MSG,
// CHECK_ONCE, CHECK_ONCE_MSG, ENSURE, ENSURE_MSG, UNREACHABLE, and DEBUG_BREAK.
//
// Implementation/configuration macros use the GAMEWIP_ASSERT_* prefix so typing
// ASSERT in IntelliSense mostly shows the actual user-facing API.

//-------------------------------------------------------------------------------------------------
// Configuration
//-------------------------------------------------------------------------------------------------

/// @def GAMEWIP_ASSERT_DIAGNOSTICS
/// @brief Controls whether expression/file/line/function/message diagnostic text is embedded in assert reports.
#ifndef GAMEWIP_ASSERT_DIAGNOSTICS
#define GAMEWIP_ASSERT_DIAGNOSTICS 1
#endif

/// @def GAMEWIP_ASSERT_POPUP_ON_ASSERT
/// @brief Controls whether failed ASSERT/VERIFY/UNREACHABLE reports may show a platform popup.
#ifndef GAMEWIP_ASSERT_POPUP_ON_ASSERT
#define GAMEWIP_ASSERT_POPUP_ON_ASSERT 1
#endif

/// @def GAMEWIP_ASSERT_POPUP_ON_CHECK
/// @brief Controls whether failed CHECK/ENSURE reports may show a platform popup.
#ifndef GAMEWIP_ASSERT_POPUP_ON_CHECK
#define GAMEWIP_ASSERT_POPUP_ON_CHECK 0
#endif

/// @def GAMEWIP_ASSERT_UNREACHABLE_ASSUME
/// @brief Controls whether disabled UNREACHABLE uses compiler unreachable assumptions instead of a trap.
#ifndef GAMEWIP_ASSERT_UNREACHABLE_ASSUME
#define GAMEWIP_ASSERT_UNREACHABLE_ASSUME 0
#endif

/// @def GAMEWIP_ASSERT_ENABLED
/// @brief Controls ASSERT/VERIFY/UNREACHABLE failure handling. Defaults to enabled when NDEBUG is not defined.
#ifndef GAMEWIP_ASSERT_ENABLED
#if !defined(NDEBUG)
#define GAMEWIP_ASSERT_ENABLED 1
#else
#define GAMEWIP_ASSERT_ENABLED 0
#endif
#endif

/// @def GAMEWIP_ASSERT_CHECKS_ENABLED
/// @brief Controls CHECK/CHECK_ONCE/ENSURE reporting. Defaults to enabled when NDEBUG is not defined.
#ifndef GAMEWIP_ASSERT_CHECKS_ENABLED
#if !defined(NDEBUG)
#define GAMEWIP_ASSERT_CHECKS_ENABLED 1
#else
#define GAMEWIP_ASSERT_CHECKS_ENABLED 0
#endif
#endif

#if (GAMEWIP_ASSERT_RUNTIME != 0) && (GAMEWIP_ASSERT_RUNTIME != 1)
#error "GAMEWIP_ASSERT_RUNTIME must be 0 or 1."
#endif

#if (GAMEWIP_ASSERT_ENABLED != 0) && (GAMEWIP_ASSERT_ENABLED != 1)
#error "GAMEWIP_ASSERT_ENABLED must be 0 or 1."
#endif

#if (GAMEWIP_ASSERT_CHECKS_ENABLED != 0) && (GAMEWIP_ASSERT_CHECKS_ENABLED != 1)
#error "GAMEWIP_ASSERT_CHECKS_ENABLED must be 0 or 1."
#endif

#if (GAMEWIP_ASSERT_DIAGNOSTICS != 0) && (GAMEWIP_ASSERT_DIAGNOSTICS != 1)
#error "GAMEWIP_ASSERT_DIAGNOSTICS must be 0 or 1."
#endif

#if (GAMEWIP_ASSERT_POPUP_ON_ASSERT != 0) && (GAMEWIP_ASSERT_POPUP_ON_ASSERT != 1)
#error "GAMEWIP_ASSERT_POPUP_ON_ASSERT must be 0 or 1."
#endif

#if (GAMEWIP_ASSERT_POPUP_ON_CHECK != 0) && (GAMEWIP_ASSERT_POPUP_ON_CHECK != 1)
#error "GAMEWIP_ASSERT_POPUP_ON_CHECK must be 0 or 1."
#endif

#if (GAMEWIP_ASSERT_UNREACHABLE_ASSUME != 0) && (GAMEWIP_ASSERT_UNREACHABLE_ASSUME != 1)
#error "GAMEWIP_ASSERT_UNREACHABLE_ASSUME must be 0 or 1."
#endif

static_assert(GAMEWIP_ASSERT_RUNTIME == 0 || GAMEWIP_ASSERT_RUNTIME == 1, "GAMEWIP_ASSERT_RUNTIME must be 0 or 1.");
static_assert(GAMEWIP_ASSERT_ENABLED == 0 || GAMEWIP_ASSERT_ENABLED == 1, "GAMEWIP_ASSERT_ENABLED must be 0 or 1.");
static_assert(GAMEWIP_ASSERT_CHECKS_ENABLED == 0 || GAMEWIP_ASSERT_CHECKS_ENABLED == 1, "GAMEWIP_ASSERT_CHECKS_ENABLED must be 0 or 1.");
static_assert(GAMEWIP_ASSERT_DIAGNOSTICS == 0 || GAMEWIP_ASSERT_DIAGNOSTICS == 1, "GAMEWIP_ASSERT_DIAGNOSTICS must be 0 or 1.");
static_assert(GAMEWIP_ASSERT_POPUP_ON_ASSERT == 0 || GAMEWIP_ASSERT_POPUP_ON_ASSERT == 1, "GAMEWIP_ASSERT_POPUP_ON_ASSERT must be 0 or 1.");
static_assert(GAMEWIP_ASSERT_POPUP_ON_CHECK == 0 || GAMEWIP_ASSERT_POPUP_ON_CHECK == 1, "GAMEWIP_ASSERT_POPUP_ON_CHECK must be 0 or 1.");
static_assert(GAMEWIP_ASSERT_UNREACHABLE_ASSUME == 0 || GAMEWIP_ASSERT_UNREACHABLE_ASSUME == 1, "GAMEWIP_ASSERT_UNREACHABLE_ASSUME must be 0 or 1.");

#if !GAMEWIP_ASSERT_RUNTIME && (GAMEWIP_ASSERT_ENABLED || GAMEWIP_ASSERT_CHECKS_ENABLED)
#error "GAMEWIP_ASSERT_RUNTIME=0 requires GAMEWIP_ASSERT_ENABLED=0 and GAMEWIP_ASSERT_CHECKS_ENABLED=0."
#endif

namespace GameWIP::Debug::Assert
{
    /// @brief Action selected for an interactive fatal assertion failure.
    ///
    /// @details Used by ASSERT_INTERACTIVE / VERIFY_INTERACTIVE failure handling.
    /// Break force-breaks into the debugger and then continues if execution resumes.
    /// Abort terminates with std::abort. IgnoreOnce continues this failure only.
    /// AlwaysIgnore continues and suppresses future failures from the same macro call site.
    enum class FailureAction
    {
        Break,
        Abort,
        IgnoreOnce,
        AlwaysIgnore
    };

#if GAMEWIP_ASSERT_RUNTIME
    /// @brief Triggers the platform debugger break instruction.
    ///
    /// @details
    /// This is the only public C++ function in the assert runtime. It is used by
    /// DEBUG_BREAK() as an explicit force-break path; fatal ASSERT failures check
    /// whether a debugger is attached before calling the platform break instruction.
    /// Most user code should prefer the macro API:
    ///
    /// - ASSERT(condition): debug assertion; compiled out when assertions are disabled.
    /// - ASSERT_MSG(condition, message): ASSERT with a custom diagnostic message.
    /// - ASSERT_INTERACTIVE(condition): debug assertion with Break/Abort/Ignore Once/Always Ignore choices.
    /// - ASSERT_INTERACTIVE_MSG(condition, message): ASSERT_INTERACTIVE with a custom diagnostic message.
    /// - VERIFY(condition): always evaluates condition; reports/breaks only when assertions are enabled.
    /// - VERIFY_MSG(condition, message): VERIFY with a custom diagnostic message.
    /// - VERIFY_INTERACTIVE(condition): always evaluates condition; reports with interactive choices when enabled.
    /// - VERIFY_INTERACTIVE_MSG(condition, message): VERIFY_INTERACTIVE with a custom diagnostic message.
    /// - CHECK(condition): recoverable report; does not break or abort.
    /// - CHECK_MSG(condition, message): CHECK with a custom diagnostic message.
    /// - CHECK_ONCE(condition): recoverable report emitted only once per call site.
    /// - CHECK_ONCE_MSG(condition, message): CHECK_ONCE with a custom diagnostic message.
    /// - ENSURE(condition): returns the condition as bool and reports false results.
    /// - ENSURE_MSG(condition, message): ENSURE with a custom diagnostic message.
    /// - UNREACHABLE(): marks code that should never execute.
    /// - DEBUG_BREAK(): always breaks into the debugger/trap path.
    ///
    /// @note Continuing from the debugger resumes execution.
    /// @see ASSERT
    /// @see ASSERT_MSG
    /// @see ASSERT_INTERACTIVE
    /// @see ASSERT_INTERACTIVE_MSG
    /// @see VERIFY
    /// @see VERIFY_INTERACTIVE
    /// @see VERIFY_INTERACTIVE_MSG
    /// @see CHECK
    /// @see ENSURE
    /// @see UNREACHABLE
    /// @see DEBUG_BREAK
    GAMEWIP_ASSERT_API void debugBreak() noexcept;
#endif
}

namespace GameWIP::Debug::Assert::Detail
{
#if GAMEWIP_ASSERT_RUNTIME
    [[noreturn]] GAMEWIP_ASSERT_API void handleAssertFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function) noexcept;

    GAMEWIP_ASSERT_API void handleInteractiveAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function,
        std::atomic_bool *alwaysIgnoreFlag) noexcept;

    GAMEWIP_ASSERT_API void handleCheckFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function) noexcept;
#endif

    inline void debugBreakInline() noexcept
    {
#if GAMEWIP_ASSERT_RUNTIME
        ::GameWIP::Debug::Assert::debugBreak();
#elif defined(_MSC_VER)
        __debugbreak();
#elif defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#else
        *static_cast<volatile int *>(nullptr) = 0;
#endif
    }

    [[noreturn]] inline void trapNoReturn() noexcept
    {
#if defined(_MSC_VER)
        __debugbreak();
        __assume(0);
#elif defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#else
        *static_cast<volatile int *>(nullptr) = 0;
        for (;;)
        {
        }
#endif
    }

    [[noreturn]] inline void unreachableHint() noexcept
    {
#if GAMEWIP_ASSERT_UNREACHABLE_ASSUME && defined(_MSC_VER)
        __assume(0);
#elif GAMEWIP_ASSERT_UNREACHABLE_ASSUME && (defined(__GNUC__) || defined(__clang__))
        __builtin_unreachable();
#else
        trapNoReturn();
#endif
    }
}

//-------------------------------------------------------------------------------------------------
// Diagnostic text helpers
//-------------------------------------------------------------------------------------------------

#if defined(__FILE_NAME__)
#define GAMEWIP_ASSERT_DETAIL_FILE_TEXT_VALUE __FILE_NAME__
#else
#define GAMEWIP_ASSERT_DETAIL_FILE_TEXT_VALUE __FILE__
#endif

#if GAMEWIP_ASSERT_DIAGNOSTICS
#define GAMEWIP_ASSERT_DETAIL_CONDITION_TEXT(condition) #condition
#define GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message) (message)
#define GAMEWIP_ASSERT_DETAIL_FILE_TEXT GAMEWIP_ASSERT_DETAIL_FILE_TEXT_VALUE
#define GAMEWIP_ASSERT_DETAIL_LINE_VALUE __LINE__
#define GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT __func__
#define GAMEWIP_ASSERT_DETAIL_UNREACHABLE_TEXT "UNREACHABLE"
#else
#define GAMEWIP_ASSERT_DETAIL_CONDITION_TEXT(condition) ""
#define GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message) ""
#define GAMEWIP_ASSERT_DETAIL_FILE_TEXT ""
#define GAMEWIP_ASSERT_DETAIL_LINE_VALUE 0
#define GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT ""
#define GAMEWIP_ASSERT_DETAIL_UNREACHABLE_TEXT ""
#endif

#define GAMEWIP_ASSERT_DETAIL_ASSERT_FAILURE_AT(condition, message, functionText) \
    ::GameWIP::Debug::Assert::Detail::handleAssertFailure(GAMEWIP_ASSERT_DETAIL_CONDITION_TEXT(condition), GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message), GAMEWIP_ASSERT_DETAIL_FILE_TEXT, GAMEWIP_ASSERT_DETAIL_LINE_VALUE, functionText)

#define GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE_AT(condition, message, functionText) \
    ::GameWIP::Debug::Assert::Detail::handleCheckFailure(GAMEWIP_ASSERT_DETAIL_CONDITION_TEXT(condition), GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message), GAMEWIP_ASSERT_DETAIL_FILE_TEXT, GAMEWIP_ASSERT_DETAIL_LINE_VALUE, functionText)

#define GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE_AT(condition, message, functionText, alwaysIgnoreFlag) \
    ::GameWIP::Debug::Assert::Detail::handleInteractiveAssertFailure(GAMEWIP_ASSERT_DETAIL_CONDITION_TEXT(condition), GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message), GAMEWIP_ASSERT_DETAIL_FILE_TEXT, GAMEWIP_ASSERT_DETAIL_LINE_VALUE, functionText, alwaysIgnoreFlag)

#define GAMEWIP_ASSERT_DETAIL_ASSERT_FAILURE(condition, message) \
    GAMEWIP_ASSERT_DETAIL_ASSERT_FAILURE_AT(condition, message, GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT)

#define GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE(condition, message, alwaysIgnoreFlag) \
    GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE_AT(condition, message, GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT, alwaysIgnoreFlag)

#define GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE(condition, message) \
    GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE_AT(condition, message, GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT)

//-------------------------------------------------------------------------------------------------
// Assertion macros
//-------------------------------------------------------------------------------------------------

#if GAMEWIP_ASSERT_ENABLED
/// @def ASSERT(condition)
/// @brief Fatal debug assertion that aborts when condition is false.
/// @param condition Boolean expression to validate.
/// @details When GAMEWIP_ASSERT_ENABLED is 1, the condition is evaluated. A false
/// result synchronously reports at Logger Fatal severity, may show the assert-owned
/// popup, breaks only when a debugger is attached, then aborts. When disabled, the
/// condition is not evaluated.
#define ASSERT(condition)                                        \
    do                                                           \
    {                                                            \
        if (!(condition)) [[unlikely]]                           \
        {                                                        \
            GAMEWIP_ASSERT_DETAIL_ASSERT_FAILURE(condition, ""); \
        }                                                        \
    } while (false)

/// @def ASSERT_MSG(condition, message)
/// @brief Fatal debug assertion with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text evaluated and embedded only when condition is false and diagnostics are enabled.
/// @details Same failure path as ASSERT. When GAMEWIP_ASSERT_DIAGNOSTICS is 0,
/// the custom message expression is not evaluated.
#define ASSERT_MSG(condition, message)                                                                    \
    do                                                                                                    \
    {                                                                                                     \
        if (!(condition)) [[unlikely]]                                                                    \
        {                                                                                                 \
            GAMEWIP_ASSERT_DETAIL_ASSERT_FAILURE(condition, GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message)); \
        }                                                                                                 \
    } while (false)

/// @def ASSERT_INTERACTIVE(condition)
/// @brief Fatal debug assertion with interactive developer failure choices.
/// @param condition Boolean expression to validate.
/// @details When assertions are enabled, the condition is evaluated unless this exact
/// macro call site has already been Always Ignored. A false result logs Fatal
/// synchronously, then lets the developer choose Break, Abort, Ignore Once, or
/// Always Ignore. Break force-calls debugBreak and continues if execution resumes;
/// Abort calls std::abort; Ignore Once continues this failure only; Always Ignore
/// suppresses future failures from this macro call site.
#define ASSERT_INTERACTIVE(condition)                                                                         \
    do                                                                                                        \
    {                                                                                                         \
        static std::atomic_bool gamewipAssertAlwaysIgnored{false};                                            \
        if (!gamewipAssertAlwaysIgnored.load(std::memory_order_relaxed))                                      \
        {                                                                                                     \
            if (!(condition)) [[unlikely]]                                                                    \
            {                                                                                                 \
                GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE(condition, "", &gamewipAssertAlwaysIgnored); \
            }                                                                                                 \
        }                                                                                                     \
    } while (false)

/// @def ASSERT_INTERACTIVE_MSG(condition, message)
/// @brief ASSERT_INTERACTIVE with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text evaluated only on failure and only when diagnostics are enabled.
#define ASSERT_INTERACTIVE_MSG(condition, message)                                                                 \
    do                                                                                                             \
    {                                                                                                              \
        static std::atomic_bool gamewipAssertAlwaysIgnored{false};                                                 \
        if (!gamewipAssertAlwaysIgnored.load(std::memory_order_relaxed))                                           \
        {                                                                                                          \
            if (!(condition)) [[unlikely]]                                                                         \
            {                                                                                                      \
                GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE(condition, message, &gamewipAssertAlwaysIgnored); \
            }                                                                                                      \
        }                                                                                                          \
    } while (false)

/// @def VERIFY(condition)
/// @brief Assertion-style check that always evaluates condition.
/// @param condition Boolean expression to evaluate.
/// @details Use VERIFY when condition has side effects that must still happen when
/// assert failure handling is disabled. In assert-enabled builds a false value uses
/// the same fatal report/break/abort path as ASSERT.
#define VERIFY(condition) ASSERT(condition)

/// @def VERIFY_MSG(condition, message)
/// @brief VERIFY with a custom diagnostic message.
/// @param condition Boolean expression to evaluate.
/// @param message Message text passed to the assert report when condition is false.
#define VERIFY_MSG(condition, message) ASSERT_MSG(condition, message)

/// @def VERIFY_INTERACTIVE(condition)
/// @brief VERIFY using the interactive fatal failure path when assertions are enabled.
/// @param condition Boolean expression to evaluate once.
/// @details The expression is always evaluated once. In assertion-enabled builds, a
/// false value logs Fatal synchronously and enters the interactive Break / Abort /
/// Ignore Once / Always Ignore path unless this call site was Always Ignored.
#define VERIFY_INTERACTIVE(condition)                                                                         \
    do                                                                                                        \
    {                                                                                                         \
        const bool gamewipAssertCondition_ = static_cast<bool>(condition);                                    \
        if (!gamewipAssertCondition_) [[unlikely]]                                                            \
        {                                                                                                     \
            static std::atomic_bool gamewipAssertAlwaysIgnored{false};                                        \
            if (!gamewipAssertAlwaysIgnored.load(std::memory_order_relaxed))                                  \
            {                                                                                                 \
                GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE(condition, "", &gamewipAssertAlwaysIgnored); \
            }                                                                                                 \
        }                                                                                                     \
    } while (false)

/// @def VERIFY_INTERACTIVE_MSG(condition, message)
/// @brief VERIFY_INTERACTIVE with a custom diagnostic message.
/// @param condition Boolean expression to evaluate once.
/// @param message Message text evaluated only on failure and only when diagnostics are enabled.
#define VERIFY_INTERACTIVE_MSG(condition, message)                                                                 \
    do                                                                                                             \
    {                                                                                                              \
        const bool gamewipAssertCondition_ = static_cast<bool>(condition);                                         \
        if (!gamewipAssertCondition_) [[unlikely]]                                                                 \
        {                                                                                                          \
            static std::atomic_bool gamewipAssertAlwaysIgnored{false};                                             \
            if (!gamewipAssertAlwaysIgnored.load(std::memory_order_relaxed))                                       \
            {                                                                                                      \
                GAMEWIP_ASSERT_DETAIL_INTERACTIVE_ASSERT_FAILURE(condition, message, &gamewipAssertAlwaysIgnored); \
            }                                                                                                      \
        }                                                                                                          \
    } while (false)

/// @def UNREACHABLE()
/// @brief Marks a code path that should never execute.
/// @details In assert-enabled builds this uses the fatal ASSERT path. When assertion
/// handling is disabled, this uses the configured trap/unreachable hint path.
#define UNREACHABLE()                                                                                                                                                                                              \
    do                                                                                                                                                                                                             \
    {                                                                                                                                                                                                              \
        ::GameWIP::Debug::Assert::Detail::handleAssertFailure(GAMEWIP_ASSERT_DETAIL_UNREACHABLE_TEXT, "", GAMEWIP_ASSERT_DETAIL_FILE_TEXT, GAMEWIP_ASSERT_DETAIL_LINE_VALUE, GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT); \
    } while (false)
#else
/// @def ASSERT(condition)
/// @brief Debug assertion compiled out when GAMEWIP_ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
#define ASSERT(condition) ((void)0)

/// @def ASSERT_MSG(condition, message)
/// @brief Debug assertion with message, compiled out when GAMEWIP_ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
/// @param message Message text; not evaluated in this mode.
#define ASSERT_MSG(condition, message) ((void)0)

/// @def ASSERT_INTERACTIVE(condition)
/// @brief Interactive debug assertion compiled out when GAMEWIP_ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
#define ASSERT_INTERACTIVE(condition) ((void)0)

/// @def ASSERT_INTERACTIVE_MSG(condition, message)
/// @brief Interactive debug assertion with message, compiled out when GAMEWIP_ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
/// @param message Message text; not evaluated in this mode.
#define ASSERT_INTERACTIVE_MSG(condition, message) ((void)0)

/// @def VERIFY(condition)
/// @brief Always evaluates condition, even when assert failure handling is disabled.
/// @param condition Boolean expression to evaluate.
#define VERIFY(condition) ((void)(condition))

/// @def VERIFY_MSG(condition, message)
/// @brief Always evaluates condition, but does not report when assert handling is disabled.
/// @param condition Boolean expression to evaluate.
/// @param message Message text; not evaluated in this mode.
#define VERIFY_MSG(condition, message) ((void)(condition))

/// @def VERIFY_INTERACTIVE(condition)
/// @brief Always evaluates condition, but does not report when assert handling is disabled.
/// @param condition Boolean expression to evaluate.
#define VERIFY_INTERACTIVE(condition) ((void)(condition))

/// @def VERIFY_INTERACTIVE_MSG(condition, message)
/// @brief Always evaluates condition, but does not report when assert handling is disabled.
/// @param condition Boolean expression to evaluate.
/// @param message Message text; not evaluated in this mode.
#define VERIFY_INTERACTIVE_MSG(condition, message) ((void)(condition))

/// @def UNREACHABLE()
/// @brief Marks a code path that should never execute.
#define UNREACHABLE() ::GameWIP::Debug::Assert::Detail::unreachableHint()
#endif

//-------------------------------------------------------------------------------------------------
// Check macros
//-------------------------------------------------------------------------------------------------

#if GAMEWIP_ASSERT_CHECKS_ENABLED
/// @def CHECK(condition)
/// @brief Recoverable check that reports when condition is false.
/// @param condition Boolean expression to validate.
/// @details Unlike ASSERT, CHECK does not break, abort, or stop execution. A false
/// result synchronously reports at Logger Error severity and then execution continues.
#define CHECK(condition)                                        \
    do                                                          \
    {                                                           \
        if (!(condition)) [[unlikely]]                          \
        {                                                       \
            GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE(condition, ""); \
        }                                                       \
    } while (false)

/// @def CHECK_MSG(condition, message)
/// @brief Recoverable check with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text passed to the check report when condition is false.
#define CHECK_MSG(condition, message)                                                                    \
    do                                                                                                   \
    {                                                                                                    \
        if (!(condition)) [[unlikely]]                                                                   \
        {                                                                                                \
            GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE(condition, GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message)); \
        }                                                                                                \
    } while (false)

/// @def CHECK_ONCE(condition)
/// @brief Recoverable check that reports only the first failure at this call site.
/// @param condition Boolean expression to validate.
/// @details The per-call-site suppression flag is thread-safe and uses relaxed atomics.
/// The flag suppresses after the first reporting attempt; it does not guarantee every
/// sink received that first report.
#define CHECK_ONCE(condition)                                                     \
    do                                                                            \
    {                                                                             \
        if (!(condition)) [[unlikely]]                                            \
        {                                                                         \
            static std::atomic_bool gamewipCheckReported_{false};                 \
            if (!gamewipCheckReported_.load(std::memory_order_relaxed) &&         \
                !gamewipCheckReported_.exchange(true, std::memory_order_relaxed)) \
            {                                                                     \
                GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE(condition, "");               \
            }                                                                     \
        }                                                                         \
    } while (false)

/// @def CHECK_ONCE_MSG(condition, message)
/// @brief CHECK_ONCE with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text passed to the first failure report at this call site.
#define CHECK_ONCE_MSG(condition, message)                                                                   \
    do                                                                                                       \
    {                                                                                                        \
        if (!(condition)) [[unlikely]]                                                                       \
        {                                                                                                    \
            static std::atomic_bool gamewipCheckReported_{false};                                            \
            if (!gamewipCheckReported_.load(std::memory_order_relaxed) &&                                    \
                !gamewipCheckReported_.exchange(true, std::memory_order_relaxed))                            \
            {                                                                                                \
                GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE(condition, GAMEWIP_ASSERT_DETAIL_MESSAGE_TEXT(message)); \
            }                                                                                                \
        }                                                                                                    \
    } while (false)

/// @def ENSURE(condition)
/// @brief Evaluates condition once, reports when false, and returns the boolean result.
/// @param condition Boolean expression to evaluate.
/// @return true when condition is true, false otherwise.
/// @details Useful for recoverable validation, for example: if (!ENSURE(load())) return false;
#define ENSURE(condition)                            \
    ([&](const char *gamewipAssertFunction_) -> bool \
     {                                                                                                         \
         const bool gamewipAssertCondition_ = static_cast<bool>(condition);                                     \
         if (!gamewipAssertCondition_) [[unlikely]]                                                            \
         {                                                                                                     \
             GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE_AT(condition, "", gamewipAssertFunction_);                    \
         }                                                                                                     \
         return gamewipAssertCondition_; }(GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT))

/// @def ENSURE_MSG(condition, message)
/// @brief ENSURE with a custom diagnostic message.
/// @param condition Boolean expression to evaluate.
/// @param message Message text passed to the check report when condition is false.
/// @return true when condition is true, false otherwise.
#define ENSURE_MSG(condition, message)               \
    ([&](const char *gamewipAssertFunction_) -> bool \
     {                                                                                                         \
         const bool gamewipAssertCondition_ = static_cast<bool>(condition);                                     \
         if (!gamewipAssertCondition_) [[unlikely]]                                                            \
         {                                                                                                     \
             GAMEWIP_ASSERT_DETAIL_CHECK_FAILURE_AT(condition, message, gamewipAssertFunction_);                \
         }                                                                                                     \
         return gamewipAssertCondition_; }(GAMEWIP_ASSERT_DETAIL_FUNCTION_TEXT))
#else
/// @def CHECK(condition)
/// @brief Recoverable check compiled out when GAMEWIP_ASSERT_CHECKS_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
#define CHECK(condition) ((void)0)

/// @def CHECK_MSG(condition, message)
/// @brief Recoverable check with message, compiled out when GAMEWIP_ASSERT_CHECKS_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
/// @param message Message text; not evaluated in this mode.
#define CHECK_MSG(condition, message) ((void)0)

/// @def CHECK_ONCE(condition)
/// @brief Recoverable once-per-call-site check compiled out when checks are disabled.
/// @param condition Boolean expression; not evaluated in this mode.
#define CHECK_ONCE(condition) ((void)0)

/// @def CHECK_ONCE_MSG(condition, message)
/// @brief CHECK_ONCE with message, compiled out when checks are disabled.
/// @param condition Boolean expression; not evaluated in this mode.
/// @param message Message text; not evaluated in this mode.
#define CHECK_ONCE_MSG(condition, message) ((void)0)

/// @def ENSURE(condition)
/// @brief Evaluates condition once and returns its boolean value when check reporting is disabled.
/// @param condition Boolean expression to evaluate.
/// @return true when condition is true, false otherwise.
#define ENSURE(condition) (static_cast<bool>(condition))

/// @def ENSURE_MSG(condition, message)
/// @brief Evaluates condition once and returns its boolean value when check reporting is disabled.
/// @param condition Boolean expression to evaluate.
/// @param message Message text; not evaluated in this mode.
/// @return true when condition is true, false otherwise.
#define ENSURE_MSG(condition, message) (static_cast<bool>(condition))
#endif

//-------------------------------------------------------------------------------------------------
// Debug break macro
//-------------------------------------------------------------------------------------------------

/// @def DEBUG_BREAK()
/// @brief Triggers a platform debugger break regardless of assert/check enablement.
/// @details This uses GameWIP::Debug::Assert::debugBreak when the runtime is available,
/// otherwise it falls back to the compiler/platform trap path in the header. Unlike
/// fatal ASSERT handling, DEBUG_BREAK() intentionally force-breaks without checking
/// whether a debugger is attached first.
#define DEBUG_BREAK()                                         \
    do                                                        \
    {                                                         \
        ::GameWIP::Debug::Assert::Detail::debugBreakInline(); \
    } while (false)
