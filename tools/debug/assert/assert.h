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
}

#ifndef NDEBUG
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