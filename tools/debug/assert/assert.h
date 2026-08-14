/// @file assert.h
/// @brief Public Assert macros and runtime support declarations.
/// @details Include this header as `debug/assert/assert.h`. The public macro API is intentionally
/// global for concise call sites; typed runtime support lives in `GameWIP::Debug::Assert`. Diagnostic
/// condition, message, file, and function text follows the project UTF-8 text contract.

#pragma once

#include <atomic>
#include <string_view>

#include "debug/assert/assert_export.h"

/// Macro families:
/// Fatal assertions use `ASSERT`, `VERIFY`, and `UNREACHABLE`. Recoverable diagnostics use
/// `CHECK`, `CHECK_ONCE`, and `ENSURE`. Developer interaction uses `ASSERT_INTERACTIVE` and
/// `VERIFY_INTERACTIVE`. Explicit debugger breaks use `DEBUG_BREAK`.

/// @def ASSERT_INTERNAL_RUNTIME
/// @brief ABI-facing build flag that tells the header whether the Assert runtime is linked.
/// @details This definition is owned by the Assert CMake target. `1` enables calls to exported
/// runtime bridge symbols; `0` selects header-only disabled behavior. Consumers should not set it manually.
#ifndef ASSERT_INTERNAL_RUNTIME
#define ASSERT_INTERNAL_RUNTIME 1
#endif

/// @def ASSERT_INTERNAL_TEST_HOOKS
/// @brief Enables source-tree-only Assert test-hook declarations for validation builds.
/// @details This definition is not installed consumer API. It exists so approved tests can include
/// `debug/assert/internal/assert_test_hooks.h` when `ASSERT_ENABLE_TEST_HOOKS` is enabled.
#ifndef ASSERT_INTERNAL_TEST_HOOKS
#define ASSERT_INTERNAL_TEST_HOOKS 0
#endif

// Public convenience macros are intentionally global:
// ASSERT, ASSERT_MSG, ASSERT_INTERACTIVE, ASSERT_INTERACTIVE_MSG, VERIFY,
// VERIFY_MSG, VERIFY_INTERACTIVE, VERIFY_INTERACTIVE_MSG, CHECK, CHECK_MSG,
// CHECK_ONCE, CHECK_ONCE_MSG, ENSURE, ENSURE_MSG, UNREACHABLE, and DEBUG_BREAK.
//
// Public configuration macros use ASSERT_*; private implementation/test vocabulary uses ASSERT_INTERNAL_*.

//-------------------------------------------------------------------------------------------------
// Configuration
//-------------------------------------------------------------------------------------------------

/// @def ASSERT_DIAGNOSTICS
/// @brief Controls whether condition, message, file, line, and function text are captured in failure reports.
/// @details When this is `0`, `_MSG` message expressions are not evaluated by Assert macros.
#ifndef ASSERT_DIAGNOSTICS
#define ASSERT_DIAGNOSTICS 1
#endif

/// @def ASSERT_POPUP_ON_ASSERT
/// @brief Controls whether fatal assertion reports may show Assert-owned platform UI.
/// @details This is compiled into the Assert runtime. Defining it only for a consumer target does
/// not reconfigure an already-built runtime library.
#ifndef ASSERT_POPUP_ON_ASSERT
#define ASSERT_POPUP_ON_ASSERT 1
#endif

/// @def ASSERT_POPUP_ON_CHECK
/// @brief Controls whether recoverable check reports may show Assert-owned platform UI.
/// @details This is compiled into the Assert runtime. Defining it only for a consumer target does
/// not reconfigure an already-built runtime library.
#ifndef ASSERT_POPUP_ON_CHECK
#define ASSERT_POPUP_ON_CHECK 0
#endif

/// @def ASSERT_UNREACHABLE_ASSUME
/// @brief Selects the disabled-build backend used by `UNREACHABLE()`.
/// @details `1` permits a compiler unreachable assumption where supported; `0` uses the trap path.
#ifndef ASSERT_UNREACHABLE_ASSUME
#define ASSERT_UNREACHABLE_ASSUME 0
#endif

/// @def ASSERT_ENABLED
/// @brief Controls fatal assertion failure handling.
/// @details This is normally propagated from the `ASSERT_ENABLED` CMake option. It affects
/// `ASSERT`, `VERIFY`, interactive fatal macros, and `UNREACHABLE`.
#ifndef ASSERT_ENABLED
#if !defined(NDEBUG)
#define ASSERT_ENABLED 1
#else
#define ASSERT_ENABLED 0
#endif
#endif

/// @def ASSERT_CHECKS_ENABLED
/// @brief Controls recoverable check reporting.
/// @details This is normally propagated from the `ASSERT_CHECKS_ENABLED` CMake option. It affects
/// `CHECK`, `CHECK_ONCE`, and `ENSURE`; `ENSURE` still evaluates and returns its condition when disabled.
#ifndef ASSERT_CHECKS_ENABLED
#if !defined(NDEBUG)
#define ASSERT_CHECKS_ENABLED 1
#else
#define ASSERT_CHECKS_ENABLED 0
#endif
#endif

#if (ASSERT_INTERNAL_RUNTIME != 0) && (ASSERT_INTERNAL_RUNTIME != 1)
#error "ASSERT_INTERNAL_RUNTIME must be 0 or 1."
#endif

#if (ASSERT_INTERNAL_TEST_HOOKS != 0) && (ASSERT_INTERNAL_TEST_HOOKS != 1)
#error "ASSERT_INTERNAL_TEST_HOOKS must be 0 or 1."
#endif

#if (ASSERT_ENABLED != 0) && (ASSERT_ENABLED != 1)
#error "ASSERT_ENABLED must be 0 or 1."
#endif

#if (ASSERT_CHECKS_ENABLED != 0) && (ASSERT_CHECKS_ENABLED != 1)
#error "ASSERT_CHECKS_ENABLED must be 0 or 1."
#endif

#if (ASSERT_DIAGNOSTICS != 0) && (ASSERT_DIAGNOSTICS != 1)
#error "ASSERT_DIAGNOSTICS must be 0 or 1."
#endif

#if (ASSERT_POPUP_ON_ASSERT != 0) && (ASSERT_POPUP_ON_ASSERT != 1)
#error "ASSERT_POPUP_ON_ASSERT must be 0 or 1."
#endif

#if (ASSERT_POPUP_ON_CHECK != 0) && (ASSERT_POPUP_ON_CHECK != 1)
#error "ASSERT_POPUP_ON_CHECK must be 0 or 1."
#endif

#if (ASSERT_UNREACHABLE_ASSUME != 0) && (ASSERT_UNREACHABLE_ASSUME != 1)
#error "ASSERT_UNREACHABLE_ASSUME must be 0 or 1."
#endif

static_assert(ASSERT_INTERNAL_RUNTIME == 0 || ASSERT_INTERNAL_RUNTIME == 1, "ASSERT_INTERNAL_RUNTIME must be 0 or 1.");
static_assert(ASSERT_INTERNAL_TEST_HOOKS == 0 || ASSERT_INTERNAL_TEST_HOOKS == 1, "ASSERT_INTERNAL_TEST_HOOKS must be 0 or 1.");
static_assert(ASSERT_ENABLED == 0 || ASSERT_ENABLED == 1, "ASSERT_ENABLED must be 0 or 1.");
static_assert(ASSERT_CHECKS_ENABLED == 0 || ASSERT_CHECKS_ENABLED == 1, "ASSERT_CHECKS_ENABLED must be 0 or 1.");
static_assert(ASSERT_DIAGNOSTICS == 0 || ASSERT_DIAGNOSTICS == 1, "ASSERT_DIAGNOSTICS must be 0 or 1.");
static_assert(ASSERT_POPUP_ON_ASSERT == 0 || ASSERT_POPUP_ON_ASSERT == 1, "ASSERT_POPUP_ON_ASSERT must be 0 or 1.");
static_assert(ASSERT_POPUP_ON_CHECK == 0 || ASSERT_POPUP_ON_CHECK == 1, "ASSERT_POPUP_ON_CHECK must be 0 or 1.");
static_assert(ASSERT_UNREACHABLE_ASSUME == 0 || ASSERT_UNREACHABLE_ASSUME == 1, "ASSERT_UNREACHABLE_ASSUME must be 0 or 1.");

#if !ASSERT_INTERNAL_RUNTIME && (ASSERT_ENABLED || ASSERT_CHECKS_ENABLED)
#error "ASSERT_INTERNAL_RUNTIME=0 requires ASSERT_ENABLED=0 and ASSERT_CHECKS_ENABLED=0."
#endif

/// @namespace GameWIP::Debug::Assert
/// @brief Runtime support for Assert macros.
/// @details Public consumers normally use the global macros. This namespace contains the small typed
/// API required for interactive actions and explicit debugger breaks.
namespace GameWIP::Debug::Assert
{
    /// @name Runtime support
    /// @{

    /// @brief Action selected after an interactive fatal assertion failure.
    ///
    /// @details `ASSERT_INTERACTIVE` and `VERIFY_INTERACTIVE` use this enum after a failed
    /// condition has been reported through Logger. `AlwaysIgnore` is scoped to the macro expansion
    /// site that owns the interactive failure.
    enum class FailureAction
    {
        /// @brief Trigger the debugger break path and continue if execution resumes.
        Break,
        /// @brief Terminate the process with std::abort().
        Abort,
        /// @brief Continue this failure only; the same call site may report again later.
        IgnoreOnce,
        /// @brief Suppress future interactive failures from the same macro call site.
        AlwaysIgnore
    };

#if ASSERT_INTERNAL_RUNTIME
    /// @brief Triggers the platform debugger break instruction.
    ///
    /// @details `DEBUG_BREAK()` calls this function when the runtime is available. Normal fatal
    /// assertion handling checks whether a debugger is attached before breaking; this function is
    /// the explicit force-break path.
    ///
    /// @note Continuing from the debugger resumes execution.
    /// @see DEBUG_BREAK
    GAMEWIP_ASSERT_EXPORT void debugBreak() noexcept;
#endif
    /// @}
} // namespace GameWIP::Debug::Assert

/// @cond ASSERT_INTERNAL_DETAIL
namespace GameWIP::Debug::Assert::Detail
{
#if ASSERT_INTERNAL_RUNTIME
    /// @brief Exported ABI bridge used by fatal public macros; not public consumer API.
    [[noreturn]] GAMEWIP_ASSERT_EXPORT void handleAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept;

    /// @brief Exported ABI bridge used by interactive public macros; not public consumer API.
    GAMEWIP_ASSERT_EXPORT void handleInteractiveAssertFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function,
        std::atomic_bool *alwaysIgnoreFlag) noexcept;

    /// @brief Exported ABI bridge used by recoverable public macros; not public consumer API.
    GAMEWIP_ASSERT_EXPORT void handleCheckFailure(
        std::string_view conditionText,
        std::string_view message,
        std::string_view file,
        int line,
        std::string_view function) noexcept;
#endif

    inline void debugBreakInline() noexcept
    {
#if ASSERT_INTERNAL_RUNTIME
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
#if ASSERT_UNREACHABLE_ASSUME && defined(_MSC_VER)
        __assume(0);
#elif ASSERT_UNREACHABLE_ASSUME && (defined(__GNUC__) || defined(__clang__))
        __builtin_unreachable();
#else
        trapNoReturn();
#endif
    }
} // namespace GameWIP::Debug::Assert::Detail

//-------------------------------------------------------------------------------------------------
// Diagnostic text helpers
//-------------------------------------------------------------------------------------------------

#if defined(__FILE_NAME__)
#define ASSERT_INTERNAL_FILE_TEXT_VALUE __FILE_NAME__
#else
#define ASSERT_INTERNAL_FILE_TEXT_VALUE __FILE__
#endif

#if ASSERT_DIAGNOSTICS
#define ASSERT_INTERNAL_CONDITION_TEXT(condition) #condition
#define ASSERT_INTERNAL_MESSAGE_TEXT(message) (message)
#define ASSERT_INTERNAL_FILE_TEXT ASSERT_INTERNAL_FILE_TEXT_VALUE
#define ASSERT_INTERNAL_LINE_VALUE __LINE__
#define ASSERT_INTERNAL_FUNCTION_TEXT __func__
#define ASSERT_INTERNAL_UNREACHABLE_TEXT "UNREACHABLE"
#else
#define ASSERT_INTERNAL_CONDITION_TEXT(condition) ""
#define ASSERT_INTERNAL_MESSAGE_TEXT(message) ""
#define ASSERT_INTERNAL_FILE_TEXT ""
#define ASSERT_INTERNAL_LINE_VALUE 0
#define ASSERT_INTERNAL_FUNCTION_TEXT ""
#define ASSERT_INTERNAL_UNREACHABLE_TEXT ""
#endif

#define ASSERT_INTERNAL_ASSERT_FAILURE_AT(condition, message, functionText) \
    ::GameWIP::Debug::Assert::Detail::handleAssertFailure( \
        ASSERT_INTERNAL_CONDITION_TEXT(condition), \
        ASSERT_INTERNAL_MESSAGE_TEXT(message), \
        ASSERT_INTERNAL_FILE_TEXT, \
        ASSERT_INTERNAL_LINE_VALUE, \
        functionText)

#define ASSERT_INTERNAL_CHECK_FAILURE_AT(condition, message, functionText) \
    ::GameWIP::Debug::Assert::Detail::handleCheckFailure( \
        ASSERT_INTERNAL_CONDITION_TEXT(condition), \
        ASSERT_INTERNAL_MESSAGE_TEXT(message), \
        ASSERT_INTERNAL_FILE_TEXT, \
        ASSERT_INTERNAL_LINE_VALUE, \
        functionText)

#define ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE_AT(condition, message, functionText, alwaysIgnoreFlag) \
    ::GameWIP::Debug::Assert::Detail::handleInteractiveAssertFailure( \
        ASSERT_INTERNAL_CONDITION_TEXT(condition), \
        ASSERT_INTERNAL_MESSAGE_TEXT(message), \
        ASSERT_INTERNAL_FILE_TEXT, \
        ASSERT_INTERNAL_LINE_VALUE, \
        functionText, \
        alwaysIgnoreFlag)

#define ASSERT_INTERNAL_ASSERT_FAILURE(condition, message) ASSERT_INTERNAL_ASSERT_FAILURE_AT(condition, message, ASSERT_INTERNAL_FUNCTION_TEXT)

#define ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE(condition, message, alwaysIgnoreFlag) \
    ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE_AT(condition, message, ASSERT_INTERNAL_FUNCTION_TEXT, alwaysIgnoreFlag)

#define ASSERT_INTERNAL_CHECK_FAILURE(condition, message) ASSERT_INTERNAL_CHECK_FAILURE_AT(condition, message, ASSERT_INTERNAL_FUNCTION_TEXT)
/// @endcond

//-------------------------------------------------------------------------------------------------
// Assertion macros
//-------------------------------------------------------------------------------------------------
/// @name Fatal assertion macros
/// @{

#if ASSERT_ENABLED
/// @def ASSERT(condition)
/// @brief Fatal debug assertion that aborts when condition is false.
/// @param condition Boolean expression to validate.
/// @details When ASSERT_ENABLED is 1, the condition is evaluated. A false
/// result synchronously reports at Logger Fatal severity, may show the assert-owned
/// popup, breaks only when a debugger is attached, then aborts. When disabled, the
/// condition is not evaluated.
#define ASSERT(condition) \
    do \
    { \
        if (!(condition)) [[unlikely]] \
        { \
            ASSERT_INTERNAL_ASSERT_FAILURE(condition, ""); \
        } \
    } while (false)

/// @def ASSERT_MSG(condition, message)
/// @brief Fatal debug assertion with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text evaluated and embedded only when condition is false and diagnostics are enabled.
/// @details Same failure path as ASSERT. When ASSERT_DIAGNOSTICS is 0,
/// the custom message expression is not evaluated.
#define ASSERT_MSG(condition, message) \
    do \
    { \
        if (!(condition)) [[unlikely]] \
        { \
            ASSERT_INTERNAL_ASSERT_FAILURE(condition, ASSERT_INTERNAL_MESSAGE_TEXT(message)); \
        } \
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
#define ASSERT_INTERACTIVE(condition) \
    do \
    { \
        static std::atomic_bool assertAlwaysIgnored{false}; \
        if (!assertAlwaysIgnored.load(std::memory_order_relaxed)) \
        { \
            if (!(condition)) [[unlikely]] \
            { \
                ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE(condition, "", &assertAlwaysIgnored); \
            } \
        } \
    } while (false)

/// @def ASSERT_INTERACTIVE_MSG(condition, message)
/// @brief ASSERT_INTERACTIVE with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text evaluated only on an unsuppressed failure and only when diagnostics are enabled.
#define ASSERT_INTERACTIVE_MSG(condition, message) \
    do \
    { \
        static std::atomic_bool assertAlwaysIgnored{false}; \
        if (!assertAlwaysIgnored.load(std::memory_order_relaxed)) \
        { \
            if (!(condition)) [[unlikely]] \
            { \
                ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE(condition, message, &assertAlwaysIgnored); \
            } \
        } \
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
/// @param message Message text evaluated only on failure and only when diagnostics are enabled.
#define VERIFY_MSG(condition, message) ASSERT_MSG(condition, message)

/// @def VERIFY_INTERACTIVE(condition)
/// @brief VERIFY using the interactive fatal failure path when assertions are enabled.
/// @param condition Boolean expression to evaluate once.
/// @details The expression is always evaluated once. In assertion-enabled builds, a
/// false value logs Fatal synchronously and enters the interactive Break / Abort /
/// Ignore Once / Always Ignore path unless this call site was Always Ignored.
#define VERIFY_INTERACTIVE(condition) \
    do \
    { \
        const bool assertCondition_ = static_cast<bool>(condition); \
        if (!assertCondition_) [[unlikely]] \
        { \
            static std::atomic_bool assertAlwaysIgnored{false}; \
            if (!assertAlwaysIgnored.load(std::memory_order_relaxed)) \
            { \
                ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE(condition, "", &assertAlwaysIgnored); \
            } \
        } \
    } while (false)

/// @def VERIFY_INTERACTIVE_MSG(condition, message)
/// @brief VERIFY_INTERACTIVE with a custom diagnostic message.
/// @param condition Boolean expression to evaluate once.
/// @param message Message text evaluated only on an unsuppressed failure and only when diagnostics are enabled.
#define VERIFY_INTERACTIVE_MSG(condition, message) \
    do \
    { \
        const bool assertCondition_ = static_cast<bool>(condition); \
        if (!assertCondition_) [[unlikely]] \
        { \
            static std::atomic_bool assertAlwaysIgnored{false}; \
            if (!assertAlwaysIgnored.load(std::memory_order_relaxed)) \
            { \
                ASSERT_INTERNAL_INTERACTIVE_ASSERT_FAILURE(condition, message, &assertAlwaysIgnored); \
            } \
        } \
    } while (false)

/// @def UNREACHABLE()
/// @brief Marks a code path that should never execute.
/// @details In assert-enabled builds this uses the fatal ASSERT path. When assertion
/// handling is disabled, this uses the configured trap/unreachable hint path.
#define UNREACHABLE() \
    do \
    { \
        ::GameWIP::Debug::Assert::Detail::handleAssertFailure( \
            ASSERT_INTERNAL_UNREACHABLE_TEXT, \
            "", \
            ASSERT_INTERNAL_FILE_TEXT, \
            ASSERT_INTERNAL_LINE_VALUE, \
            ASSERT_INTERNAL_FUNCTION_TEXT); \
    } while (false)
#else
/// @def ASSERT(condition)
/// @brief Debug assertion compiled out when ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
#define ASSERT(condition) ((void)0)

/// @def ASSERT_MSG(condition, message)
/// @brief Debug assertion with message, compiled out when ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
/// @param message Message text; not evaluated in this mode.
#define ASSERT_MSG(condition, message) ((void)0)

/// @def ASSERT_INTERACTIVE(condition)
/// @brief Interactive debug assertion compiled out when ASSERT_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
#define ASSERT_INTERACTIVE(condition) ((void)0)

/// @def ASSERT_INTERACTIVE_MSG(condition, message)
/// @brief Interactive debug assertion with message, compiled out when ASSERT_ENABLED is 0.
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
/// @}

//-------------------------------------------------------------------------------------------------
// Check macros
//-------------------------------------------------------------------------------------------------
/// @name Recoverable check macros
/// @{

#if ASSERT_CHECKS_ENABLED
/// @def CHECK(condition)
/// @brief Recoverable check that reports when condition is false.
/// @param condition Boolean expression to validate.
/// @details Unlike ASSERT, CHECK does not break, abort, or stop execution. A false
/// result synchronously reports at Logger Error severity and then execution continues.
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) [[unlikely]] \
        { \
            ASSERT_INTERNAL_CHECK_FAILURE(condition, ""); \
        } \
    } while (false)

/// @def CHECK_MSG(condition, message)
/// @brief Recoverable check with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text evaluated only on failure and only when diagnostics are enabled.
#define CHECK_MSG(condition, message) \
    do \
    { \
        if (!(condition)) [[unlikely]] \
        { \
            ASSERT_INTERNAL_CHECK_FAILURE(condition, ASSERT_INTERNAL_MESSAGE_TEXT(message)); \
        } \
    } while (false)

/// @def CHECK_ONCE(condition)
/// @brief Recoverable check that reports only the first failure at this call site.
/// @param condition Boolean expression to validate.
/// @details The per-call-site suppression flag is thread-safe and uses relaxed atomics.
/// The flag suppresses after the first reporting attempt; it does not guarantee every
/// sink received that first report.
#define CHECK_ONCE(condition) \
    do \
    { \
        if (!(condition)) [[unlikely]] \
        { \
            static std::atomic_bool assertCheckReported_{false}; \
            if (!assertCheckReported_.load(std::memory_order_relaxed) && !assertCheckReported_.exchange(true, std::memory_order_relaxed)) \
            { \
                ASSERT_INTERNAL_CHECK_FAILURE(condition, ""); \
            } \
        } \
    } while (false)

/// @def CHECK_ONCE_MSG(condition, message)
/// @brief CHECK_ONCE with a custom diagnostic message.
/// @param condition Boolean expression to validate.
/// @param message Message text evaluated only for the first reported failure at this call site and only when diagnostics are enabled.
#define CHECK_ONCE_MSG(condition, message) \
    do \
    { \
        if (!(condition)) [[unlikely]] \
        { \
            static std::atomic_bool assertCheckReported_{false}; \
            if (!assertCheckReported_.load(std::memory_order_relaxed) && !assertCheckReported_.exchange(true, std::memory_order_relaxed)) \
            { \
                ASSERT_INTERNAL_CHECK_FAILURE(condition, ASSERT_INTERNAL_MESSAGE_TEXT(message)); \
            } \
        } \
    } while (false)

/// @def ENSURE(condition)
/// @brief Evaluates condition once, reports when false, and returns the boolean result.
/// @param condition Boolean expression to evaluate.
/// @return true when condition is true, false otherwise.
/// @details Useful for recoverable validation, for example: if (!ENSURE(load())) return false;
#define ENSURE(condition) \
    ( \
        [&](const char *assertFunction_) -> bool \
        { \
            const bool assertCondition_ = static_cast<bool>(condition); \
            if (!assertCondition_) [[unlikely]] \
            { \
                ASSERT_INTERNAL_CHECK_FAILURE_AT(condition, "", assertFunction_); \
            } \
            return assertCondition_; \
        }(ASSERT_INTERNAL_FUNCTION_TEXT))

/// @def ENSURE_MSG(condition, message)
/// @brief ENSURE with a custom diagnostic message.
/// @param condition Boolean expression to evaluate.
/// @param message Message text evaluated only on false results and only when diagnostics are enabled.
/// @return true when condition is true, false otherwise.
#define ENSURE_MSG(condition, message) \
    ( \
        [&](const char *assertFunction_) -> bool \
        { \
            const bool assertCondition_ = static_cast<bool>(condition); \
            if (!assertCondition_) [[unlikely]] \
            { \
                ASSERT_INTERNAL_CHECK_FAILURE_AT(condition, message, assertFunction_); \
            } \
            return assertCondition_; \
        }(ASSERT_INTERNAL_FUNCTION_TEXT))
#else
/// @def CHECK(condition)
/// @brief Recoverable check compiled out when ASSERT_CHECKS_ENABLED is 0.
/// @param condition Boolean expression; not evaluated in this mode.
#define CHECK(condition) ((void)0)

/// @def CHECK_MSG(condition, message)
/// @brief Recoverable check with message, compiled out when ASSERT_CHECKS_ENABLED is 0.
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
/// @}

//-------------------------------------------------------------------------------------------------
// Debug break macro
//-------------------------------------------------------------------------------------------------
/// @name Debug break macro
/// @{

/// @def DEBUG_BREAK()
/// @brief Triggers a platform debugger break regardless of assert/check enablement.
/// @details This uses GameWIP::Debug::Assert::debugBreak when the runtime is available,
/// otherwise it falls back to the compiler/platform trap path in the header. Unlike
/// fatal ASSERT handling, DEBUG_BREAK() intentionally force-breaks without checking
/// whether a debugger is attached first.
#define DEBUG_BREAK() \
    do \
    { \
        ::GameWIP::Debug::Assert::Detail::debugBreakInline(); \
    } while (false)
/// @}
