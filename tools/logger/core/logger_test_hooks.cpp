/// @file logger_test_hooks.cpp
/// @brief Logger internal test hook definitions.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
#if GAMEWIP_LOGGER_TEST_HOOKS
    LoggerTestHookState loggerTestHookState;

    bool consumeTestHook(std::atomic_bool &flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }

    void resetLoggerTestHooks() noexcept
    {
        loggerTestHookState.nextFileOpenFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileWriteFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileFlushFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextQueueAllocationFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFatalPopupFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextTimedFlushTimeout.store(false, std::memory_order_release);
    }
#endif
}

using namespace GameWIP::Logger::Detail::Core;

#if GAMEWIP_LOGGER_TEST_HOOKS
namespace GameWIP::Logger::TestHooks
{
    void reset() noexcept
    {
        resetLoggerTestHooks();
    }

    void forceNextFileOpenFailure() noexcept
    {
        loggerTestHookState.nextFileOpenFailure.store(true, std::memory_order_release);
    }

    void forceNextFileWriteFailure() noexcept
    {
        loggerTestHookState.nextFileWriteFailure.store(true, std::memory_order_release);
    }

    void forceNextFileFlushFailure() noexcept
    {
        loggerTestHookState.nextFileFlushFailure.store(true, std::memory_order_release);
    }

    void forceNextQueueAllocationFailure() noexcept
    {
        loggerTestHookState.nextQueueAllocationFailure.store(true, std::memory_order_release);
    }

    void forceNextFatalPopupFailure() noexcept
    {
        loggerTestHookState.nextFatalPopupFailure.store(true, std::memory_order_release);
    }

    void forceNextTimedFlushTimeout() noexcept
    {
        loggerTestHookState.nextTimedFlushTimeout.store(true, std::memory_order_release);
    }
}
#endif
