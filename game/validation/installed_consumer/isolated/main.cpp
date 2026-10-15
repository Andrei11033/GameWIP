/// @file main.cpp
/// @brief Isolated installed-package dependency-discovery check.

#if defined(ASSERT_INTERNAL_TEST_HOOKS) || defined(FILESYSTEM_INTERNAL_TEST_HOOKS) || defined(TERMINAL_INTERNAL_TEST_HOOKS) || \
    defined(LOGGER_INTERNAL_TEST_HOOKS) || defined(DESKTOP_INTERNAL_TEST_HOOKS)
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
#include "desktop/drag_drop.h"
#include "desktop/display_info.h"
#include "desktop/renderer_bridge.h"
#include "desktop/window.h"
bool probeInstalledAccessibility();
bool probeInstalledDragDrop();
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
    const auto capabilities = GameWIP::Desktop::getCapabilities();
    const GameWIP::Desktop::Types::Result<GameWIP::Desktop::Cursor> cursor =
        GameWIP::Desktop::createCursor(std::span<const GameWIP::Desktop::Types::Cursor::ImageView>{});
    const auto feedback = GameWIP::Desktop::Renderer::attachOcclusionProvider(window);
    const auto displayColor = GameWIP::Desktop::Display::getColorInfo(window);
    return probeInstalledAccessibility() && probeInstalledDragDrop() && capabilities.ok() &&
                   cursor.status.code == GameWIP::IO::Types::ErrorCode::InvalidArgument && !cursor.value.has_value() &&
                   !GameWIP::Desktop::Renderer::hasOcclusionProvider(window) && feedback.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   displayColor.status.code == GameWIP::IO::Types::ErrorCode::NotOpen
               ? 0
               : 1;
#elif defined(GAMEWIP_CONSUMER_Logger)
    static_cast<void>(GameWIP::Logger::defaultConfig());
    return 0;
#elif defined(GAMEWIP_CONSUMER_Assert)
#if !ASSERT_ENABLED && !ASSERT_CHECKS_ENABLED
    int conditionEvaluations = 0;
    int messageEvaluations = 0;
    const auto condition = [&]
    {
        ++conditionEvaluations;
        return true;
    };
    [[maybe_unused]] const auto message = [&]
    {
        ++messageEvaluations;
        return "must remain lazy";
    };
    ASSERT(condition());
    ASSERT_MSG(condition(), message());
    ASSERT_INTERACTIVE(condition());
    ASSERT_INTERACTIVE_MSG(condition(), message());
    CHECK(condition());
    CHECK_MSG(condition(), message());
    CHECK_ONCE(condition());
    CHECK_ONCE_MSG(condition(), message());
    if (conditionEvaluations != 0 || messageEvaluations != 0)
    {
        return 1;
    }
    VERIFY(condition());
    VERIFY_MSG(condition(), message());
    VERIFY_INTERACTIVE(condition());
    VERIFY_INTERACTIVE_MSG(condition(), message());
    const bool ensured = ENSURE(condition());
    const bool ensuredWithMessage = ENSURE_MSG(condition(), message());
    const bool rejected = ENSURE_MSG(false, message());
    return conditionEvaluations == 6 && messageEvaluations == 0 && ensured && ensuredWithMessage && !rejected ? 0 : 1;
#else
    CHECK(true);
    return 0;
#endif
#elif defined(GAMEWIP_CONSUMER_TestSupport)
    GameWIP::TestSupport::Timer timer;
    const GameWIP::TestSupport::Types::InfrastructureStatus status;
    const GameWIP::TestSupport::Types::Process::Result childResult;
    const std::string statusText = GameWIP::TestSupport::formatInfrastructureStatus(status);
    GameWIP::TestSupport::Types::Reporting::Options reportingOptions;
    reportingOptions.writeConsole = false;
    reportingOptions.writeReport = false;
    GameWIP::TestSupport::Runner runner(reportingOptions);
    const auto runnerInitialReport = runner.reportStatus();
    const auto runnerFinalReport = runner.finalize();
    GameWIP::TestSupport::Context context("installed", reportingOptions);
    const auto contextFinalReport = context.finalize();
    const bool defaultsAreUsable = status.ok() && statusText == "None" && childResult.status.ok() &&
                                   childResult.outcome == GameWIP::TestSupport::Types::Process::Outcome::NotStarted &&
                                   childResult.outputBytes.empty() &&
                                   runnerInitialReport.state == GameWIP::TestSupport::Types::Reporting::SinkState::Disabled &&
                                   runnerFinalReport.state == GameWIP::TestSupport::Types::Reporting::SinkState::Disabled &&
                                   contextFinalReport.state == GameWIP::TestSupport::Types::Reporting::SinkState::Disabled;
    return timer.elapsedMilliseconds() >= 0.0 && defaultsAreUsable ? 0 : 1;
#elif defined(__INTELLISENSE__)
    GameWIP::Desktop::Window window;
    return GameWIP::Desktop::Renderer::attachOcclusionProvider(window).code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   GameWIP::Desktop::Display::getColorInfo(window).status.code == GameWIP::IO::Types::ErrorCode::NotOpen
               ? 0
               : 1;
#endif
}
