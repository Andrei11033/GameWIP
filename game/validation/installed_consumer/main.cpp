/// @file main.cpp
/// @brief Clean installed-package consumer boundary check.

#if defined(IO_INTERNAL_TEST_HOOKS) || defined(FILESYSTEM_INTERNAL_TEST_HOOKS) || defined(TERMINAL_INTERNAL_TEST_HOOKS) || \
    defined(LOGGER_INTERNAL_TEST_HOOKS) || defined(ASSERT_INTERNAL_TEST_HOOKS) || defined(TEST_SUPPORT_INTERNAL_TEST_HOOKS) || \
    defined(WINDOW_INTERNAL_TEST_HOOKS)
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
#include "window/display_info.h"
#include "window/renderer_bridge.h"
#include "window/window.h"

#include <string>
#include <string_view>

int main()
{
    const GameWIP::Unicode::Types::Version unicodeVersion = GameWIP::Unicode::getStandardVersion();
    const GameWIP::Unicode::Types::Utf8::EncodeResult unicodeEncoding = GameWIP::Unicode::Utf8::encodeScalar(static_cast<char32_t>(0x1F600));
    GameWIP::IO::MemoryWriter writer;
    const GameWIP::IO::Types::Status reserve = writer.reserve(64);
    const GameWIP::IO::Types::WriteResult write = GameWIP::IO::writeAllText(writer, "installed consumer");
    const GameWIP::IO::Types::CopyTextResult text = writer.copyText();
    const GameWIP::FileSystem::Types::PathResult path = GameWIP::FileSystem::pathFromUtf8("installed-consumer.txt");
    const GameWIP::FileSystem::Types::File::ReadOptions filesystemReadOptions{};
    const GameWIP::Terminal::Types::Output::CapabilitiesResult capabilities = GameWIP::Terminal::getOutputCapabilities();

    GameWIP::Terminal::OutputBuffer terminalBuffer;
    const GameWIP::IO::Types::Status terminalBufferLineEnding = terminalBuffer.setLineEnding(GameWIP::Terminal::Types::Output::LineEnding::Lf);
    const GameWIP::IO::Types::Status terminalBufferReserve = terminalBuffer.reserve(64);
    const GameWIP::IO::Types::Status terminalBufferAppend = terminalBuffer.appendLine("installed terminal buffer");
    const GameWIP::IO::Types::Status terminalBufferPrint = terminalBuffer.print("{} {}", "formatted", 7);
    const GameWIP::IO::Types::Status terminalBufferPrintln = terminalBuffer.println(" {}", "line");

    GameWIP::Terminal::Session closedTerminalSession;
    const GameWIP::IO::Types::Status closedSessionWrite = closedTerminalSession.writeText("must-not-write");
    const GameWIP::IO::Types::Status closedSessionPrint = closedTerminalSession.print("must-not-write {}", 1);
    const GameWIP::IO::Types::Status closedSessionPrintln = closedTerminalSession.println("must-not-write {}", 2);
    const GameWIP::IO::Types::Status invalidDirectPrint =
        GameWIP::Terminal::print(static_cast<GameWIP::Terminal::Types::Output::Stream>(-1), "{}", 1);
    const GameWIP::IO::Types::Status invalidDirectPrintln =
        GameWIP::Terminal::println(static_cast<GameWIP::Terminal::Types::Output::Stream>(-1), "{}", 2);

    const GameWIP::Logger::Types::Config loggerConfig = GameWIP::Logger::defaultConfig();
    GameWIP::TestSupport::Timer timer;
    const GameWIP::TestSupport::Types::InfrastructureStatus infrastructureStatus;
    const GameWIP::TestSupport::Types::Process::Result childResult;
    const GameWIP::TestSupport::Types::Reporting::Options reportingOptions;
    const std::string infrastructureText = GameWIP::TestSupport::formatInfrastructureStatus(infrastructureStatus);
    const GameWIP::Window::Types::CapabilitiesResult windowCapabilities = GameWIP::Window::getCapabilities();
    const GameWIP::Window::Types::LogicalSize windowSize{640, 360};
    GameWIP::Window::Window closedWindow;
    const GameWIP::IO::Types::Status rendererFeedbackStatus = GameWIP::Window::Renderer::attachOcclusionProvider(closedWindow);
    const bool rendererProvider = GameWIP::Window::Renderer::hasOcclusionProvider(closedWindow);
    const GameWIP::Window::Types::Display::ColorInfoResult displayColor = GameWIP::Window::Display::getColorInfo(closedWindow);

    CHECK(write.status.ok());
    CHECK(text.status.ok());
    CHECK(path.status.ok());
    static_cast<void>(capabilities);
    static_cast<void>(filesystemReadOptions);
    static_cast<void>(timer.elapsedMilliseconds());
    static_cast<void>(windowCapabilities);

    return unicodeVersion.major == 17 && unicodeVersion.minor == 0 && unicodeVersion.patch == 0 &&
                   unicodeEncoding.outcome == GameWIP::Unicode::Types::EncodeOutcome::Encoded && unicodeEncoding.byteCount == 4 && reserve.ok() &&
                   write.status.ok() && text.status.ok() && text.text == "installed consumer" && path.status.ok() && terminalBufferLineEnding.ok() &&
                   terminalBufferReserve.ok() && terminalBufferAppend.ok() && terminalBufferPrint.ok() && terminalBufferPrintln.ok() &&
                   terminalBuffer.text() == std::string_view{"installed terminal buffer\nformatted 7 line\n"} &&
                   closedSessionWrite.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   closedSessionPrint.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   closedSessionPrintln.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   invalidDirectPrint.code == GameWIP::IO::Types::ErrorCode::InvalidArgument &&
                   invalidDirectPrintln.code == GameWIP::IO::Types::ErrorCode::InvalidArgument && infrastructureStatus.ok() &&
                   infrastructureText == "None" && childResult.status.ok() &&
                   childResult.outcome == GameWIP::TestSupport::Types::Process::Outcome::NotStarted && childResult.outputBytes.empty() &&
                   reportingOptions.writeConsole && rendererFeedbackStatus.code == GameWIP::IO::Types::ErrorCode::NotOpen && !rendererProvider &&
                   displayColor.status.code == GameWIP::IO::Types::ErrorCode::NotOpen && windowSize.width == 640 &&
                   loggerConfig.logDirectory == std::string_view{"logs"}
               ? 0
               : 1;
}
