/// @file main.cpp
/// @brief Isolated installed-package dependency-discovery check.

#if defined(INTERNAL_IO_TEST_HOOKS) || defined(INTERNAL_FILESYSTEM_TEST_HOOKS) || defined(INTERNAL_TERMINAL_TEST_HOOKS) || \
    defined(INTERNAL_LOGGER_TEST_HOOKS) || defined(INTERNAL_ASSERT_TEST_HOOKS) || defined(INTERNAL_TEST_SUPPORT_TEST_HOOKS) || \
    defined(INTERNAL_WINDOW_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#if defined(GAMEWIP_CONSUMER_Unicode)
#include "unicode/unicode.h"
#elif defined(GAMEWIP_CONSUMER_IO)
#include "io/io.h"
#elif defined(GAMEWIP_CONSUMER_FileSystem)
#include "filesystem/filesystem.h"
#elif defined(GAMEWIP_CONSUMER_Terminal)
#include "terminal/terminal.h"
#elif defined(GAMEWIP_CONSUMER_Window)
#include "window/renderer.h"
#include "window/window.h"
#elif defined(GAMEWIP_CONSUMER_Logger)
#include "logger/logger.h"
#elif defined(GAMEWIP_CONSUMER_Assert)
#include "debug/assert/assert.h"
#elif defined(GAMEWIP_CONSUMER_TestSupport)
#include "test_support/test_support.h"
#elif defined(__INTELLISENSE__)
// CMake compiles this source once per selected package. The standalone editor parse has no
// selection, so give IntelliSense a representative branch without weakening the real-build guard.
#include "window/renderer.h"
#include "window/window.h"
#else
#error "An isolated consumer package must be selected."
#endif

#include <string>

int main()
{
#if defined(GAMEWIP_CONSUMER_Unicode)
    const GameWIP::Unicode::Types::UnicodeVersion version =
        GameWIP::Unicode::getStandardVersion();
    const GameWIP::Unicode::Types::Utf8EncodeResult encoded =
        GameWIP::Unicode::Utf8::encodeScalar(static_cast<char32_t>(0x1F600));

    return version.major == 17 && version.minor == 0 && version.patch == 0 &&
                   encoded.outcome == GameWIP::Unicode::Types::EncodeOutcome::Encoded &&
                   encoded.byteCount == 4
               ? 0
               : 1;
#elif defined(GAMEWIP_CONSUMER_IO)
    GameWIP::IO::MemoryWriter writer;
    const auto reserve = writer.reserve(32);
    const auto write = GameWIP::IO::writeAllText(writer, "isolated");
    const auto text = writer.copyText();
    return reserve.ok() && write.status.ok() && text.status.ok() && text.text == "isolated" ? 0 : 1;
#elif defined(GAMEWIP_CONSUMER_FileSystem)
    return GameWIP::FileSystem::pathFromUtf8("isolated.txt").status.ok() ? 0 : 1;
#elif defined(GAMEWIP_CONSUMER_Terminal)
    static_cast<void>(GameWIP::Terminal::getOutputCapabilities());
    return 0;
#elif defined(GAMEWIP_CONSUMER_Window)
    GameWIP::Window::Window window;
    const auto feedback = GameWIP::Window::Renderer::attachOcclusionProvider(window);
    const auto displayColor = GameWIP::Window::Renderer::getWindowDisplayColorInfo(window);
    return GameWIP::Window::getCapabilities().status.ok() && feedback.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   displayColor.status.code == GameWIP::IO::Types::ErrorCode::NotOpen
               ? 0
               : 1;
#elif defined(GAMEWIP_CONSUMER_Logger)
    static_cast<void>(GameWIP::Logger::defaultConfig());
    return 0;
#elif defined(GAMEWIP_CONSUMER_Assert)
    CHECK(true);
    return 0;
#elif defined(GAMEWIP_CONSUMER_TestSupport)
    GameWIP::TestSupport::Timer timer;
    const GameWIP::TestSupport::Types::InfrastructureStatus status;
    const GameWIP::TestSupport::Types::ChildProcessResult childResult;
    const std::string statusText = GameWIP::TestSupport::formatInfrastructureStatus(status);
    const bool defaultsAreUsable = status.ok() && statusText == "None" && childResult.status.ok() &&
                                   childResult.outcome == GameWIP::TestSupport::Types::ChildProcessOutcome::NotStarted;
    return timer.elapsedMilliseconds() >= 0.0 && defaultsAreUsable ? 0 : 1;
#elif defined(__INTELLISENSE__)
    GameWIP::Window::Window window;
    return GameWIP::Window::Renderer::attachOcclusionProvider(window).code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   GameWIP::Window::Renderer::getWindowDisplayColorInfo(window).status.code == GameWIP::IO::Types::ErrorCode::NotOpen
               ? 0
               : 1;
#endif
}
