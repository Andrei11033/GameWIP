#if defined(INTERNAL_FILESYSTEM_TEST_HOOKS) || defined(INTERNAL_TERMINAL_TEST_HOOKS) || defined(INTERNAL_LOGGER_TEST_HOOKS) || \
    defined(INTERNAL_ASSERT_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#include "debug/assert/assert.h"
#include "filesystem/filesystem.h"
#include "io/io.h"
#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "terminal/terminal.h"
#include "test_support/test_support.h"

#include <string_view>

int main()
{
    GameWIP::IO::MemoryWriter writer;
    const GameWIP::IO::Types::WriteResult write = GameWIP::IO::writeAllText(writer, "installed consumer");
    const GameWIP::FileSystem::Types::PathResult path = GameWIP::FileSystem::pathFromUtf8("installed-consumer.txt");
    const GameWIP::Terminal::Types::OutputCapabilitiesResult capabilities = GameWIP::Terminal::getOutputCapabilities();
    const GameWIP::Logger::Types::Config loggerConfig = GameWIP::Logger::defaultConfig();
    GameWIP::TestSupport::Timer timer;

    CHECK(write.status.ok());
    CHECK(path.status.ok());
    static_cast<void>(capabilities);
    static_cast<void>(timer.elapsedMilliseconds());

    return write.status.ok() && path.status.ok() && loggerConfig.logDirectory == std::string_view{"logs"} ? 0 : 1;
}
