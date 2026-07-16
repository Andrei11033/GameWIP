/// @file main.cpp
/// @brief Isolated installed-package dependency-discovery check.

#if defined(INTERNAL_FILESYSTEM_TEST_HOOKS) || defined(INTERNAL_TERMINAL_TEST_HOOKS) || defined(INTERNAL_LOGGER_TEST_HOOKS) || \
    defined(INTERNAL_ASSERT_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#if defined(GAMEWIP_CONSUMER_IO)
#include "io/io.h"
#elif defined(GAMEWIP_CONSUMER_FileSystem)
#include "filesystem/filesystem.h"
#elif defined(GAMEWIP_CONSUMER_Terminal)
#include "terminal/terminal.h"
#elif defined(GAMEWIP_CONSUMER_Logger)
#include "logger/logger.h"
#elif defined(GAMEWIP_CONSUMER_Assert)
#include "debug/assert/assert.h"
#elif defined(GAMEWIP_CONSUMER_TestSupport)
#include "test_support/test_support.h"
#else
#error "An isolated consumer package must be selected."
#endif

int main()
{
#if defined(GAMEWIP_CONSUMER_IO)
    GameWIP::IO::MemoryWriter writer;
    return GameWIP::IO::writeAllText(writer, "isolated").status.ok() ? 0 : 1;
#elif defined(GAMEWIP_CONSUMER_FileSystem)
    return GameWIP::FileSystem::pathFromUtf8("isolated.txt").status.ok() ? 0 : 1;
#elif defined(GAMEWIP_CONSUMER_Terminal)
    static_cast<void>(GameWIP::Terminal::getOutputCapabilities());
    return 0;
#elif defined(GAMEWIP_CONSUMER_Logger)
    static_cast<void>(GameWIP::Logger::defaultConfig());
    return 0;
#elif defined(GAMEWIP_CONSUMER_Assert)
    CHECK(true);
    return 0;
#elif defined(GAMEWIP_CONSUMER_TestSupport)
    GameWIP::TestSupport::Timer timer;
    return timer.elapsedMilliseconds() >= 0.0 ? 0 : 1;
#endif
}
