/// @file main.cpp
/// @brief Clean installed-package consumer boundary check.
///
/// This executable is built from the installed package surface. It verifies that
/// public headers are usable together and that internal test-hook definitions do
/// not leak through installed imported targets. It is a package-boundary smoke
/// test, not a replacement for each library's behavior validation suite.

#if defined(INTERNAL_FILESYSTEM_TEST_HOOKS) || defined(INTERNAL_TERMINAL_TEST_HOOKS) || defined(INTERNAL_LOGGER_TEST_HOOKS) || \
    defined(INTERNAL_ASSERT_TEST_HOOKS) || defined(INTERNAL_WINDOW_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#include "debug/assert/assert.h"
#include "filesystem/filesystem.h"
#include "io/io.h"
#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "terminal/terminal.h"
#include "test_support/test_support.h"
#include "window/integration/renderer_feedback.h"
#include "window/window.h"

#include <string_view>

int main()
{
    GameWIP::IO::MemoryWriter writer;
    const GameWIP::IO::Types::WriteResult write = GameWIP::IO::writeAllText(writer, "installed consumer");
    const GameWIP::FileSystem::Types::PathResult path = GameWIP::FileSystem::pathFromUtf8("installed-consumer.txt");
    const GameWIP::Terminal::Types::OutputCapabilitiesResult capabilities = GameWIP::Terminal::getOutputCapabilities();
    const GameWIP::Logger::Types::Config loggerConfig = GameWIP::Logger::defaultConfig();
    GameWIP::TestSupport::Timer timer;
    const GameWIP::Window::Types::CapabilitiesResult windowCapabilities = GameWIP::Window::getCapabilities();
    const GameWIP::Window::Types::Size windowSize{640, 360};
    GameWIP::Window::Window closedWindow;
    const GameWIP::IO::Types::Status rendererFeedbackStatus = GameWIP::Window::Integration::Renderer::attachOcclusionProvider(closedWindow);

    // Exercise the installed Assert macro surface through a normal consumer
    // target. The detailed behavior is covered by the source-tree Assert tests.
    CHECK(write.status.ok());
    CHECK(path.status.ok());
    static_cast<void>(capabilities);
    static_cast<void>(timer.elapsedMilliseconds());
    static_cast<void>(windowCapabilities);

    return write.status.ok() && path.status.ok() && rendererFeedbackStatus.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   windowSize.width == 640 && loggerConfig.logDirectory == std::string_view{"logs"}
               ? 0
               : 1;
}
