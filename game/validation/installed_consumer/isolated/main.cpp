/// @file main.cpp
/// @brief Isolated installed-package dependency-discovery check.

#if defined(IO_INTERNAL_TEST_HOOKS) || defined(FILESYSTEM_INTERNAL_TEST_HOOKS) || defined(TERMINAL_INTERNAL_TEST_HOOKS) || \
    defined(LOGGER_INTERNAL_TEST_HOOKS) || defined(ASSERT_INTERNAL_TEST_HOOKS) || defined(TEST_SUPPORT_INTERNAL_TEST_HOOKS) || \
    defined(DESKTOP_INTERNAL_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#if defined(GAMEWIP_CONSUMER_Unicode)
#include "unicode/unicode.h"
#elif defined(GAMEWIP_CONSUMER_IO)
#include "io/status.h"
#include "io/stream.h"
#include "io/io.h"
#elif defined(GAMEWIP_CONSUMER_FileSystem)
#include "filesystem/path.h"
#include "filesystem/file.h"
#include "filesystem/filesystem.h"
#elif defined(GAMEWIP_CONSUMER_Terminal)
#include "terminal/input.h"
#include "terminal/output.h"
#include "terminal/session.h"
#include "terminal/terminal.h"
#elif defined(GAMEWIP_CONSUMER_Desktop)
#include "desktop/cursor.h"
#include "desktop/display_info.h"
#include "desktop/renderer_bridge.h"
#include "desktop/window.h"
#elif defined(GAMEWIP_CONSUMER_Logger)
#include "logger/logger.h"
#elif defined(GAMEWIP_CONSUMER_Assert)
#include "debug/assert/assert.h"
#elif defined(GAMEWIP_CONSUMER_TestSupport)
#include "test_support/test_support.h"
#elif defined(__INTELLISENSE__)
#include "desktop/cursor.h"
#include "desktop/display_info.h"
#include "desktop/renderer_bridge.h"
#include "desktop/window.h"
#else
#error "An isolated consumer package must be selected."
#endif

#include <span>

#include <string>

int main()
{
#if defined(GAMEWIP_CONSUMER_Unicode)
    const GameWIP::Unicode::Types::Version version = GameWIP::Unicode::getStandardVersion();
    const GameWIP::Unicode::Types::Utf8::EncodeResult encoded = GameWIP::Unicode::Utf8::encodeScalar(static_cast<char32_t>(0x1F600));
    return version.major == 17 && version.minor == 0 && version.patch == 0 && encoded.outcome == GameWIP::Unicode::Types::EncodeOutcome::Encoded &&
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
    const GameWIP::FileSystem::Types::File::ReadOptions options{};
    return GameWIP::FileSystem::pathFromUtf8("isolated.txt").status.ok() && options.bufferSize > 0 ? 0 : 1;
#elif defined(GAMEWIP_CONSUMER_Terminal)
    static_cast<void>(GameWIP::Terminal::getOutputCapabilities());
    return 0;
#elif defined(GAMEWIP_CONSUMER_Desktop)
    GameWIP::Desktop::Window window;
    const GameWIP::Desktop::Types::Cursor::CreateResult cursor =
        GameWIP::Desktop::createCursor(std::span<const GameWIP::Desktop::Types::Cursor::ImageView>{});
    const auto feedback = GameWIP::Desktop::Renderer::attachOcclusionProvider(window);
    const auto displayColor = GameWIP::Desktop::Display::getColorInfo(window);
    return GameWIP::Desktop::getCapabilities().status.ok() && cursor.status.code == GameWIP::IO::Types::ErrorCode::InvalidArgument &&
                   !cursor.cursor.isValid() && !GameWIP::Desktop::Renderer::hasOcclusionProvider(window) &&
                   feedback.code == GameWIP::IO::Types::ErrorCode::NotOpen && displayColor.status.code == GameWIP::IO::Types::ErrorCode::NotOpen
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
    const GameWIP::TestSupport::Types::Process::Result childResult;
    const std::string statusText = GameWIP::TestSupport::formatInfrastructureStatus(status);
    const bool defaultsAreUsable = status.ok() && statusText == "None" && childResult.status.ok() &&
                                   childResult.outcome == GameWIP::TestSupport::Types::Process::Outcome::NotStarted &&
                                   childResult.outputBytes.empty();
    return timer.elapsedMilliseconds() >= 0.0 && defaultsAreUsable ? 0 : 1;
#elif defined(__INTELLISENSE__)
    GameWIP::Desktop::Window window;
    return GameWIP::Desktop::Renderer::attachOcclusionProvider(window).code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   GameWIP::Desktop::Display::getColorInfo(window).status.code == GameWIP::IO::Types::ErrorCode::NotOpen
               ? 0
               : 1;
#endif
}
