/// @file main.cpp
/// @brief Clean installed-package consumer boundary check.

#if defined(IO_INTERNAL_TEST_HOOKS) || defined(FILESYSTEM_INTERNAL_TEST_HOOKS) || defined(TERMINAL_INTERNAL_TEST_HOOKS) || \
    defined(LOGGER_INTERNAL_TEST_HOOKS) || defined(ASSERT_INTERNAL_TEST_HOOKS) || defined(TEST_SUPPORT_INTERNAL_TEST_HOOKS) || \
    defined(DESKTOP_INTERNAL_TEST_HOOKS)
#error "Installed GameWIP targets must not expose internal test-hook compile definitions."
#endif

#include "io/status.h"
#include "io/stream.h"
#include "filesystem/path.h"
#include "filesystem/file.h"
#include "terminal/input.h"
#include "terminal/output.h"
#include "terminal/session.h"

#include "debug/assert/assert.h"
#include "filesystem/filesystem.h"
#include "io/io.h"
#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "terminal/terminal.h"
#include "test_support/test_support.h"
#include "unicode/unicode.h"
#include "desktop/child_surface.h"
#include "desktop/clipboard.h"
#include "desktop/cursor.h"
#include "desktop/data_transfer.h"
#include "desktop/display_info.h"
#include "desktop/renderer_bridge.h"
#include "desktop/window.h"

#include <span>
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
    const GameWIP::Desktop::Types::CapabilitiesResult windowCapabilities = GameWIP::Desktop::getCapabilities();
    const GameWIP::Desktop::Types::Clipboard::FormatResult clipboardText =
        GameWIP::Desktop::Clipboard::hasFormat({GameWIP::Desktop::Types::DataTransfer::FormatKind::Text, {}});
    const GameWIP::Desktop::Types::Cursor::CreateResult invalidCursor =
        GameWIP::Desktop::createCursor(std::span<const GameWIP::Desktop::Types::Cursor::ImageView>{});
    const GameWIP::Desktop::Types::Cursor::CreateResult invalidSingleCursor =
        GameWIP::Desktop::createCursor(GameWIP::Desktop::Types::Cursor::ImageView{});
    const GameWIP::Desktop::Types::LogicalSize windowSize{640, 360};
    GameWIP::Desktop::Window closedWindow;
    GameWIP::Desktop::ChildSurface closedChildSurface;
    const GameWIP::IO::Types::Status rendererFeedbackStatus = GameWIP::Desktop::Renderer::attachOcclusionProvider(closedWindow);
    const bool rendererProvider = GameWIP::Desktop::Renderer::hasOcclusionProvider(closedWindow);
    const GameWIP::Desktop::Types::Display::ColorInfoResult displayColor = GameWIP::Desktop::Display::getColorInfo(closedWindow);

    CHECK(write.status.ok());
    CHECK(text.status.ok());
    CHECK(path.status.ok());
    static_cast<void>(capabilities);
    static_cast<void>(filesystemReadOptions);
    static_cast<void>(timer.elapsedMilliseconds());
    static_cast<void>(windowCapabilities);
    static_cast<void>(clipboardText);

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
                   reportingOptions.writeConsole && invalidCursor.status.code == GameWIP::IO::Types::ErrorCode::InvalidArgument &&
                   !invalidCursor.cursor.isValid() && invalidSingleCursor.status.code == GameWIP::IO::Types::ErrorCode::InvalidArgument &&
                   !invalidSingleCursor.cursor.isValid() && rendererFeedbackStatus.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
                   !rendererProvider && displayColor.status.code == GameWIP::IO::Types::ErrorCode::NotOpen && windowSize.width == 640 &&
                   loggerConfig.logDirectory == std::string_view{"logs"} && !closedChildSurface.isOpen()
               ? 0
               : 1;
}
