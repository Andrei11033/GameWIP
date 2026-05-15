#pragma once

#include <string_view>

namespace GameWIP::Debug
{
    /// @brief Handles failed assertions.
    /// @param conditionText the expression that failed, as text.
    /// @param message optional custom message, empty when using plain assert.
    /// @param file source file where the assert failed.
    /// @param line source line where the assert failed.
    /// @param function where the assert failed.
    void handleAssertFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function);

    /// @brief Handles failed recoverable checks.
    /// @param conditionText the expression that failed, as text.
    /// @param message optional custom message, empty when using plain check.
    /// @param file source file where the check failed.
    /// @param line source line where the check failed.
    /// @param function where the check failed.
    void handleCheckFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function);
}

#if defined(GAMEWIP_ASSERTS_ALWAYS_ON) && defined(GAMEWIP_ASSERTS_ALWAYS_OFF)
#error "GAMEWIP_ASSERTS_ALWAYS_ON and GAMEWIP_ASSERTS_ALWAYS_OFF cannot both be defined."
#endif

#if defined(GAMEWIP_CHECKS_ALWAYS_ON) && defined(GAMEWIP_CHECKS_ALWAYS_OFF)
#error "GAMEWIP_CHECKS_ALWAYS_ON and GAMEWIP_CHECKS_ALWAYS_OFF cannot both be defined."
#endif

#if defined(GAMEWIP_ASSERTS_ALWAYS_ON)
#define GAMEWIP_ASSERTS_ENABLED 1
#elif defined(GAMEWIP_ASSERTS_ALWAYS_OFF)
#define GAMEWIP_ASSERTS_ENABLED 0
#elif !defined(NDEBUG)
#define GAMEWIP_ASSERTS_ENABLED 1
#else
#define GAMEWIP_ASSERTS_ENABLED 0
#endif

#if defined(GAMEWIP_CHECKS_ALWAYS_ON)
#define GAMEWIP_CHECKS_ENABLED 1
#elif defined(GAMEWIP_CHECKS_ALWAYS_OFF)
#define GAMEWIP_CHECKS_ENABLED 0
#elif !defined(NDEBUG)
#define GAMEWIP_CHECKS_ENABLED 1
#else
#define GAMEWIP_CHECKS_ENABLED 0
#endif

#if GAMEWIP_ASSERTS_ENABLED
#define GAMEWIP_ASSERT(condition)                                                              \
    do                                                                                         \
    {                                                                                          \
        if (!(condition))                                                                      \
        {                                                                                      \
            GameWIP::Debug::handleAssertFailure(#condition, "", __FILE__, __LINE__, __func__); \
        }                                                                                      \
    } while (false)

#define GAMEWIP_ASSERT_MSG(condition, message)                                                      \
    do                                                                                              \
    {                                                                                               \
        if (!(condition))                                                                           \
        {                                                                                           \
            GameWIP::Debug::handleAssertFailure(#condition, message, __FILE__, __LINE__, __func__); \
        }                                                                                           \
    } while (false)
#else
#define GAMEWIP_ASSERT(condition) ((void)0)
#define GAMEWIP_ASSERT_MSG(condition, message) ((void)0)
#endif

#if GAMEWIP_CHECKS_ENABLED
#define GAMEWIP_CHECK(condition)                                                              \
    do                                                                                        \
    {                                                                                         \
        if (!(condition))                                                                     \
        {                                                                                     \
            GameWIP::Debug::handleCheckFailure(#condition, "", __FILE__, __LINE__, __func__); \
        }                                                                                     \
    } while (false)

#define GAMEWIP_CHECK_MSG(condition, message)                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            GameWIP::Debug::handleCheckFailure(#condition, message, __FILE__, __LINE__, __func__); \
        }                                                                                          \
    } while (false)
#else
#define GAMEWIP_CHECK(condition) ((void)0)
#define GAMEWIP_CHECK_MSG(condition, message) ((void)0)
#endif
