/// @file main.cpp
/// @brief Clean installed-package consumer boundary check.
///
/// This executable is built from the installed package surface. It verifies that
/// public headers are usable together and that internal test-hook definitions do
/// not leak through installed imported targets. It is a package-boundary smoke
/// test, not a replacement for each library's behavior validation suite.

#if defined(INTERNAL_IO_TEST_HOOKS) || defined(INTERNAL_FILESYSTEM_TEST_HOOKS) || defined(INTERNAL_TERMINAL_TEST_HOOKS) || \
    defined(INTERNAL_LOGGER_TEST_HOOKS) || defined(INTERNAL_ASSERT_TEST_HOOKS) || defined(INTERNAL_TEST_SUPPORT_TEST_HOOKS) || \
    defined(INTERNAL_WINDOW_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#include "debug/assert/assert.h"
#include "filesystem/filesystem.h"
#include "io/io.h"
#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "terminal/terminal.h"
#include "test_support/test_support.h"
#include "unicode/unicode.h"
#include "window/renderer.h"
#include "window/window.h"

#include <string>
#include <string_view>

int main()
{
    const GameWIP::Unicode::Types::UnicodeVersion unicodeVersion = GameWIP::Unicode::getStandardVersion();
    const GameWIP::Unicode::Types::Utf8EncodeResult unicodeEncoding = GameWIP::Unicode::Utf8::encodeScalar(static_cast<char32_t>(0x1F600));
    GameWIP::IO::MemoryWriter writer;
    const GameWIP::IO::Types::Status reserve = writer.reserve(64);
    const GameWIP::IO::Types::WriteResult write = GameWIP::IO::writeAllText(writer, "installed consumer");
    const GameWIP::IO::Types::TextCopyResult text = writer.copyText();
    const GameWIP::FileSystem::Types::PathResult path = GameWIP::FileSystem::pathFromUtf8("installed-consumer.txt");
    const GameWIP::Terminal::Types::OutputCapabilitiesResult capabilities = GameWIP::Terminal::getOutputCapabilities();
    const GameWIP::Logger::Types::Config loggerConfig = GameWIP::Logger::defaultConfig();
    GameWIP::TestSupport::Timer timer;
    const GameWIP::TestSupport::Types::InfrastructureStatus infrastructureStatus;
    const GameWIP::TestSupport::Types::ChildProcessResult childResult;
    const std::string infrastructureText = GameWIP::TestSupport::formatInfrastructureStatus(infrastructureStatus);
    const GameWIP::Window::Types::CapabilitiesResult windowCapabilities = GameWIP::Window::getCapabilities();
    const GameWIP::Window::Types::LogicalSize windowSize{640, 360};
    GameWIP::Window::Window closedWindow;
    const GameWIP::IO::Types::Status rendererFeedbackStatus = GameWIP::Window::Renderer::attachOcclusionProvider(closedWindow);
    const GameWIP::Window::Types::DisplayColorInfoResult displayColor = GameWIP::Window::Renderer::getWindowDisplayColorInfo(closedWindow);

    // Exercise the installed Assert macro surface through a normal consumer
    // target. The detailed behavior is covered by the source-tree Assert tests.
    CHECK(write.status.ok());
    CHECK(text.status.ok());
    CHECK(path.status.ok());
    static_cast<void>(capabilities);
    static_cast<void>(timer.elapsedMilliseconds());
    static_cast<void>(windowCapabilities);

    return unicodeVersion.major == 17 && unicodeVersion.minor == 0 && unicodeVersion.patch == 0 &&
                   unicodeEncoding.outcome == GameWIP::Unicode::Types::EncodeOutcome::Encoded && unicodeEncoding.byteCount == 4 && reserve.ok() &&
                   write.status.ok() && text.status.ok() && text.text == "installed consumer" && path.status.ok() && infrastructureStatus.ok() &&
                   infrastructureText == "None" && childResult.status.ok() &&
                   childResult.outcome == GameWIP::TestSupport::Types::ChildProcessOutcome::NotStarted &&
                   rendererFeedbackStatus.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   displayColor.status.code == GameWIP::IO::Types::ErrorCode::NotOpen && windowSize.width == 640 &&
                   loggerConfig.logDirectory == std::string_view{"logs"}
               ? 0
               : 1;
}
