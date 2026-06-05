/// @file terminal_test.cpp
/// @brief Executable self-tests for the GameWIP Terminal library.

#include "test/terminal_test.h"

#include "terminal/terminal.h"
#include "test_support/test_support.h"

#ifndef GAMEWIP_TERMINAL_TEST_HOOKS
#define GAMEWIP_TERMINAL_TEST_HOOKS 0
#endif

#if GAMEWIP_TERMINAL_TEST_HOOKS
#include "terminal/internal/terminal_test_hooks.h"
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace IO = GameWIP::IO;
    namespace Terminal = GameWIP::Terminal;
    namespace TestSupport = GameWIP::TestSupport;

    using ErrorCode = IO::Types::ErrorCode;
    using TerminalTestOptions = GameWIP::Test::TerminalTestOptions;

    [[nodiscard]] std::span<const std::byte> bytesOf(std::string_view text)
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    [[nodiscard]] std::vector<std::byte> copyBytes(std::string_view text)
    {
        const std::span<const std::byte> bytes = bytesOf(text);
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    void testPassiveHelpers(TestSupport::Context &context)
    {
        const Terminal::Types::Color defaultColor = Terminal::defaultColor();
        static_cast<void>(context.expectEq("defaultColor kind", Terminal::Types::ColorKind::Default, defaultColor.kind));

        const Terminal::Types::Color basic = Terminal::basicColor(Terminal::Types::BasicColor::BrightCyan);
        static_cast<void>(context.expectEq("basicColor kind", Terminal::Types::ColorKind::Basic, basic.kind));
        static_cast<void>(context.expectEq("basicColor value", Terminal::Types::BasicColor::BrightCyan, basic.basic));

        const Terminal::Types::Color rgb = Terminal::rgbColor(1, 2, 3);
        static_cast<void>(context.expectEq("rgbColor kind", Terminal::Types::ColorKind::Rgb, rgb.kind));
        static_cast<void>(context.expectEq("rgbColor red", std::uint8_t{1}, rgb.red));
        static_cast<void>(context.expectEq("rgbColor green", std::uint8_t{2}, rgb.green));
        static_cast<void>(context.expectEq("rgbColor blue", std::uint8_t{3}, rgb.blue));

        const Terminal::Types::InputMode interactive = Terminal::makeInputMode(Terminal::Types::InputModePreset::InteractiveLine);
        static_cast<void>(context.expectTrue("interactive mode line buffered", interactive.lineBuffered));
        static_cast<void>(context.expectTrue("interactive mode echo", interactive.echoInput));
        static_cast<void>(context.expectTrue("interactive mode control keys", interactive.processControlKeys));

        const Terminal::Types::InputMode raw = Terminal::makeInputMode(Terminal::Types::InputModePreset::RawBytes);
        static_cast<void>(context.expectFalse("raw mode not line buffered", raw.lineBuffered));
        static_cast<void>(context.expectFalse("raw mode no echo", raw.echoInput));
        static_cast<void>(context.expectFalse("raw mode no control keys", raw.processControlKeys));

        const Terminal::Types::WriteSegment text = Terminal::textSegment("text");
        static_cast<void>(context.expectEq("text segment kind", Terminal::Types::WriteSegmentKind::Text, text.kind));
        static_cast<void>(context.expectEq("text segment view", std::string_view{"text"}, text.text));

        Terminal::Types::TextStyle style;
        style.bold = true;
        const Terminal::Types::WriteSegment styled = Terminal::styledSegment("styled", style);
        static_cast<void>(context.expectEq("styled segment kind", Terminal::Types::WriteSegmentKind::StyledText, styled.kind));
        static_cast<void>(context.expectTrue("styled segment stores style", styled.style.bold));

        const std::string byteText = "bytes";
        const Terminal::Types::WriteSegment bytes = Terminal::byteSegment(bytesOf(byteText));
        static_cast<void>(context.expectEq("byte segment kind", Terminal::Types::WriteSegmentKind::Bytes, bytes.kind));
        static_cast<void>(context.expectEq("byte segment size", byteText.size(), bytes.bytes.size()));
    }

#if GAMEWIP_TERMINAL_TEST_HOOKS
    namespace Hooks = GameWIP::Terminal::TestHooks;

    [[nodiscard]] Terminal::Types::StyleCapabilities allStyleCapabilities() noexcept
    {
        return {
            .basicColor = true,
            .rgbColor = true,
            .bold = true,
            .dim = true,
            .italic = true,
            .underline = true,
            .inverse = true,
            .strikethrough = true};
    }

    [[nodiscard]] Terminal::Types::OutputCapabilities terminalOutputCapabilities() noexcept
    {
        return {
            .kind = Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteOutput = true,
            .supportsFlush = true,
            .style = allStyleCapabilities(),
            .supportsTerminalSize = true,
            .supportsCursorMovement = true,
            .supportsCursorPositionQuery = true,
            .supportsCursorSaveRestore = true,
            .supportsCursorVisibility = true,
            .supportsClear = true,
            .supportsScroll = true,
            .supportsAlternateScreen = true,
            .supportsTitle = true,
            .supportsBell = true};
    }

    [[nodiscard]] Terminal::Types::OutputCapabilities redirectedOutputCapabilities() noexcept
    {
        return {.kind = Terminal::Types::StreamKind::Redirected, .supportsUtf8Text = true, .supportsByteOutput = true, .supportsFlush = true};
    }

    [[nodiscard]] Terminal::Types::InputCapabilities terminalInputCapabilities() noexcept
    {
        return {
            .kind = Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteInput = true,
            .supportsLineInput = true,
            .supportsRawInput = true,
            .supportsEchoControl = true,
            .supportsInputMode = true,
            .supportsInputAvailability = true,
            .supportsReadTimeout = true};
    }

    void setupCapturedOutput(Terminal::Types::OutputStream stream, Terminal::Types::OutputCapabilities capabilities = terminalOutputCapabilities())
    {
        Hooks::setOutputCapabilitiesOverride(stream, capabilities);
        Hooks::setOutputCapture(stream, true);
        Hooks::clearCapturedOutput(stream);
    }

    void setupInput(std::string_view bytes, bool endOfStreamWhenEmpty = true)
    {
        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, bytes, endOfStreamWhenEmpty);
    }

    void testCapabilitiesAndQueries(TestSupport::Context &context)
    {
        Hooks::reset();

        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
        const Terminal::Types::InputCapabilityResult inputCapabilities = Terminal::getInputCapabilities();
        static_cast<void>(context.expectTrue("input capabilities status", inputCapabilities.status.ok()));
        static_cast<void>(context.expectEq("input capability kind", Terminal::Types::StreamKind::Terminal, inputCapabilities.capabilities.kind));
        static_cast<void>(context.expectTrue("input capability mode support", inputCapabilities.capabilities.supportsInputMode));

        Hooks::forceNextInputCapabilityFailure(ErrorCode::PermissionDenied);
        static_cast<void>(
            context.expectEq("input capability forced failure", ErrorCode::PermissionDenied, Terminal::getInputCapabilities().status.code));

        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
        const Terminal::Types::OutputCapabilityResult outputCapabilities = Terminal::getOutputCapabilities();
        static_cast<void>(context.expectTrue("output capabilities status", outputCapabilities.status.ok()));
        static_cast<void>(context.expectTrue("output style capability", outputCapabilities.capabilities.style.rgbColor));
        static_cast<void>(context.expectTrue("output cursor capability", outputCapabilities.capabilities.supportsCursorMovement));

        Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectEq("output capability forced failure", ErrorCode::StatFailed, Terminal::getOutputCapabilities().status.code));

        Hooks::setTerminalSizeOverride(Terminal::Types::OutputStream::Stdout, {.columns = 120, .rows = 40});
        const Terminal::Types::TerminalSizeResult size = Terminal::getTerminalSize();
        static_cast<void>(context.expectTrue("terminal size status", size.status.ok()));
        static_cast<void>(context.expectEq("terminal size columns", std::uint32_t{120}, size.size.columns));
        static_cast<void>(context.expectEq("terminal size rows", std::uint32_t{40}, size.size.rows));

        Hooks::setCursorPositionOverride(Terminal::Types::OutputStream::Stdout, {.column = 7, .row = 9});
        const Terminal::Types::CursorPositionResult position = Terminal::getCursorPosition();
        static_cast<void>(context.expectTrue("cursor position status", position.status.ok()));
        static_cast<void>(context.expectEq("cursor position column", std::uint32_t{7}, position.position.column));
        static_cast<void>(context.expectEq("cursor position row", std::uint32_t{9}, position.position.row));

        Hooks::forceNextTerminalSizeFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectEq("terminal size forced failure", ErrorCode::StatFailed, Terminal::getTerminalSize().status.code));

        Hooks::forceNextCursorPositionFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectEq("cursor position forced failure", ErrorCode::StatFailed, Terminal::getCursorPosition().status.code));

        Hooks::reset();
    }

    void testTextAndStyleOutput(TestSupport::Context &context)
    {
        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);

        Terminal::Types::LineWriteOptions plainOptions;
        plainOptions.lineEnding = Terminal::Types::LineEnding::Lf;
        plainOptions.flushMode = IO::Types::FlushMode::Data;
        static_cast<void>(context.expectTrue("plain write succeeds", Terminal::writeLine("hello", plainOptions).ok()));
        static_cast<void>(
            context.expectEq("plain write capture", std::string{"hello\n"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Types::TextStyle style;
        style.foreground = Terminal::basicColor(Terminal::Types::BasicColor::BrightRed);
        style.bold = true;

        Terminal::Types::TextWriteOptions styledOptions;
        styledOptions.style = style;
        styledOptions.styleMode = Terminal::Types::StyleMode::Auto;
        static_cast<void>(context.expectTrue("styled write succeeds", Terminal::writeText("hot", styledOptions).ok()));
        static_cast<void>(context.expectEq(
            "styled write emits SGR and reset",
            std::string{"\x1b[1;91mhot\x1b[0m"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, redirectedOutputCapabilities());
        static_cast<void>(context.expectTrue("auto style fallback succeeds", Terminal::writeText("plain", styledOptions).ok()));
        static_cast<void>(
            context.expectEq("auto style fallback is plain", std::string{"plain"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        styledOptions.styleMode = Terminal::Types::StyleMode::Always;
        static_cast<void>(context.expectEq("forced unsupported style fails", ErrorCode::Unsupported, Terminal::writeText("fail", styledOptions).code));
        static_cast<void>(
            context.expectTrue("forced unsupported style writes nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("resetStyle succeeds", Terminal::resetStyle().ok()));
        static_cast<void>(
            context.expectEq("resetStyle emits reset", std::string{"\x1b[0m"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::forceNextTextWriteFailure(ErrorCode::PermissionDenied);
        static_cast<void>(context.expectEq("forced text write failure", ErrorCode::PermissionDenied, Terminal::writeText("blocked").code));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("formatted print succeeds", Terminal::print("value {}", 42).ok()));
        static_cast<void>(
            context.expectEq("formatted print capture", std::string{"value 42"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Types::LineWriteOptions printLineOptions;
        printLineOptions.lineEnding = Terminal::Types::LineEnding::Lf;
        static_cast<void>(context.expectTrue("formatted println succeeds", Terminal::println(printLineOptions, "line {}", 7).ok()));
        static_cast<void>(
            context.expectEq("formatted println capture", std::string{"line 7\n"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::OutputBuffer outputBuffer(Terminal::Types::LineEnding::Lf);
        outputBuffer.reserve(64);
        outputBuffer.appendText("alpha");
        outputBuffer.appendLine(" beta");
        outputBuffer.print("{}", 3);
        outputBuffer.println(" {}", 4);
        static_cast<void>(context.expectEq("output buffer text", std::string_view{"alpha beta\n3 4\n"}, outputBuffer.text()));

        Terminal::Writer stdoutWriter;
        static_cast<void>(context.expectTrue("output buffer flush succeeds", outputBuffer.flushTo(stdoutWriter).ok()));
        static_cast<void>(context.expectTrue("output buffer clears after flush", outputBuffer.empty()));
        static_cast<void>(
            context.expectEq("output buffer flush capture", std::string{"alpha beta\n3 4\n"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("writer formatted print succeeds", stdoutWriter.print("writer {}", 5).ok()));
        static_cast<void>(context.expectEq(
            "writer formatted print capture",
            std::string{"writer 5"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("writer formatted println succeeds", stdoutWriter.println(printLineOptions, "writer line {}", 6).ok()));
        static_cast<void>(context.expectEq(
            "writer formatted println capture",
            std::string{"writer line 6\n"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::reset();
    }

    void testSegmentedAndByteOutput(TestSupport::Context &context)
    {
        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);

        Terminal::Types::TextStyle style;
        style.bold = true;
        const std::string byteText = "c";
        const std::array<Terminal::Types::WriteSegment, 3> segments{
            Terminal::textSegment("a"),
            Terminal::styledSegment("b", style),
            Terminal::byteSegment(bytesOf(byteText))};

        Terminal::Types::SegmentWriteOptions options;
        options.appendLineEnding = true;
        options.lineEnding = Terminal::Types::LineEnding::Lf;
        static_cast<void>(
            context.expectTrue("segmented write succeeds", Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(segments), options).ok()));
        static_cast<void>(context.expectEq(
            "segmented write preserves order",
            std::string{"a\x1b[1mb\x1b[0mc\n"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        const std::string raw = "raw";
        const IO::Types::WriteResult rawWrite = Terminal::writeBytes(bytesOf(raw));
        static_cast<void>(context.expectTrue("byte write succeeds", rawWrite.status.ok()));
        static_cast<void>(context.expectEq("byte write count", raw.size(), rawWrite.bytesWritten));
        static_cast<void>(context.expectEq("byte write capture bytes", copyBytes(raw), Hooks::capturedOutput(Terminal::Types::OutputStream::Stdout)));

        Hooks::forceNextByteWriteFailure(ErrorCode::BrokenPipe);
        const IO::Types::WriteResult failedWrite = Terminal::writeBytes(bytesOf(raw));
        static_cast<void>(context.expectEq("forced byte write failure", ErrorCode::BrokenPipe, failedWrite.status.code));
        static_cast<void>(context.expectEq("forced byte write reports zero", std::size_t{0}, failedWrite.bytesWritten));

        Hooks::reset();
    }

    void testControls(TestSupport::Context &context)
    {
        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);

        static_cast<void>(context.expectTrue("move cursor succeeds", Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Up, 2).ok()));
        static_cast<void>(context.expectTrue("set cursor succeeds", Terminal::setCursorPosition({.column = 4, .row = 2}).ok()));
        static_cast<void>(context.expectTrue("save cursor succeeds", Terminal::saveCursorPosition().ok()));
        static_cast<void>(context.expectTrue("restore cursor succeeds", Terminal::restoreCursorPosition().ok()));
        static_cast<void>(context.expectTrue("hide cursor succeeds", Terminal::setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("show cursor succeeds", Terminal::setCursorVisible(true).ok()));
        static_cast<void>(context.expectTrue("clear succeeds", Terminal::clear(Terminal::Types::ClearTarget::EntireScreenAndScrollback).ok()));
        static_cast<void>(context.expectTrue("scroll succeeds", Terminal::scroll(Terminal::Types::ScrollDirection::Down, 3).ok()));
        static_cast<void>(context.expectTrue("enter alt succeeds", Terminal::enterAlternateScreen().ok()));
        static_cast<void>(context.expectTrue("leave alt succeeds", Terminal::leaveAlternateScreen().ok()));
        static_cast<void>(context.expectTrue(
            "title succeeds",
            Terminal::setTitle(
                "A\x1b"
                "B\a"
                "C")
                .ok()));
        static_cast<void>(context.expectTrue("bell succeeds", Terminal::ringBell().ok()));

        static_cast<void>(context.expectEq(
            "control sequence capture",
            std::string{"\x1b[2A\x1b[3;5H\x1b[s\x1b[u\x1b[?25l\x1b[?25h\x1b[3J\x1b[3T\x1b[?1049h\x1b[?1049l\x1b]0;A B C\a\a"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, redirectedOutputCapabilities());
        static_cast<void>(context.expectEq(
            "unsupported cursor movement fails",
            ErrorCode::Unsupported,
            Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 1).code));
        static_cast<void>(context.expectTrue(
            "unsupported cursor movement writes nothing",
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));
        static_cast<void>(context.expectTrue("zero move is no-op success", Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 0).ok()));
        static_cast<void>(context.expectEq(
            "zero move still validates direction",
            ErrorCode::InvalidArgument,
            Terminal::moveCursor(static_cast<Terminal::Types::CursorMoveDirection>(-1), 0).code));
        static_cast<void>(context.expectEq(
            "zero scroll still validates direction",
            ErrorCode::InvalidArgument,
            Terminal::scroll(static_cast<Terminal::Types::ScrollDirection>(-1), 0).code));

        Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
        static_cast<void>(context.expectEq(
            "forced flush failure through control",
            ErrorCode::FlushFailed,
            Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 0, {.flushMode = IO::Types::FlushMode::Data}).code));

        Hooks::reset();
    }

    void testInputReads(TestSupport::Context &context)
    {
        Hooks::reset();

        setupInput("abcd");
        std::array<std::byte, 4> buffer{};
        Terminal::Types::ByteReadOptions byteOptions;
        byteOptions.timeout = Terminal::kNoWait;
        byteOptions.allowPartial = false;
        const Terminal::Types::ByteReadResult bytes = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
        static_cast<void>(context.expectTrue("byte read status", bytes.status.ok()));
        static_cast<void>(context.expectEq("byte read outcome", Terminal::Types::ReadOutcome::Completed, bytes.outcome));
        static_cast<void>(context.expectEq("byte read count", buffer.size(), bytes.bytesRead));
        static_cast<void>(context.expectEq("byte read contents", copyBytes("abcd"), std::vector<std::byte>(buffer.begin(), buffer.end())));

        const Terminal::Types::ByteReadResult eof = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
        static_cast<void>(context.expectTrue("byte EOF status", eof.status.ok()));
        static_cast<void>(context.expectEq("byte EOF outcome", Terminal::Types::ReadOutcome::EndOfStream, eof.outcome));

        setupInput("one\r\ntwo\rc");
        Terminal::Types::LineReadOptions keepOptions;
        keepOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::Keep;
        const Terminal::Types::LineReadResult firstLine = Terminal::readLine(keepOptions);
        static_cast<void>(context.expectTrue("first line status", firstLine.status.ok()));
        static_cast<void>(context.expectEq("first line text", std::string{"one\r\n"}, firstLine.line));
        static_cast<void>(context.expectEq("first line ending", Terminal::Types::ConsumedLineEnding::CrLf, firstLine.consumedLineEnding));

        Terminal::Types::LineReadOptions normalizeOptions;
        normalizeOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::NormalizeToLf;
        const Terminal::Types::LineReadResult secondLine = Terminal::readLine(normalizeOptions);
        static_cast<void>(context.expectTrue("second line status", secondLine.status.ok()));
        static_cast<void>(context.expectEq("second line text", std::string{"two\n"}, secondLine.line));
        static_cast<void>(context.expectEq("second line ending", Terminal::Types::ConsumedLineEnding::Cr, secondLine.consumedLineEnding));

        const Terminal::Types::LineReadResult finalLine = Terminal::readLine();
        static_cast<void>(context.expectTrue("final line status", finalLine.status.ok()));
        static_cast<void>(context.expectEq("final line text", std::string{"c"}, finalLine.line));
        static_cast<void>(context.expectEq("final line outcome", Terminal::Types::ReadOutcome::EndOfStream, finalLine.outcome));

        setupInput("abc\nnext\n");
        Terminal::Types::LineReadOptions keepLimitedLfOptions;
        keepLimitedLfOptions.maxBytes = 3;
        keepLimitedLfOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::Keep;
        const Terminal::Types::LineReadResult keepLimitedLf = Terminal::readLine(keepLimitedLfOptions);
        static_cast<void>(context.expectTrue("limited LF keep status", keepLimitedLf.status.ok()));
        static_cast<void>(context.expectEq("limited LF keep text", std::string{"abc"}, keepLimitedLf.line));
        static_cast<void>(context.expectEq("limited LF keep size", std::size_t{3}, keepLimitedLf.line.size()));
        static_cast<void>(context.expectTrue("limited LF keep reports truncation", keepLimitedLf.wasTruncated));
        static_cast<void>(context.expectEq("limited LF keep ending consumed", Terminal::Types::ConsumedLineEnding::Lf, keepLimitedLf.consumedLineEnding));
        const Terminal::Types::LineReadResult afterLimitedLf = Terminal::readLine();
        static_cast<void>(context.expectTrue("after limited LF status", afterLimitedLf.status.ok()));
        static_cast<void>(context.expectEq("after limited LF text", std::string{"next"}, afterLimitedLf.line));

        setupInput("abc\r\nnext\n");
        Terminal::Types::LineReadOptions keepLimitedCrLfOptions;
        keepLimitedCrLfOptions.maxBytes = 4;
        keepLimitedCrLfOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::Keep;
        const Terminal::Types::LineReadResult keepLimitedCrLf = Terminal::readLine(keepLimitedCrLfOptions);
        static_cast<void>(context.expectTrue("limited CRLF keep status", keepLimitedCrLf.status.ok()));
        static_cast<void>(context.expectEq("limited CRLF keep text avoids partial ending", std::string{"abc"}, keepLimitedCrLf.line));
        static_cast<void>(context.expectTrue("limited CRLF keep reports truncation", keepLimitedCrLf.wasTruncated));
        static_cast<void>(context.expectEq(
            "limited CRLF keep ending consumed",
            Terminal::Types::ConsumedLineEnding::CrLf,
            keepLimitedCrLf.consumedLineEnding));
        const Terminal::Types::LineReadResult afterLimitedCrLf = Terminal::readLine();
        static_cast<void>(context.expectTrue("after limited CRLF status", afterLimitedCrLf.status.ok()));
        static_cast<void>(context.expectEq("after limited CRLF text", std::string{"next"}, afterLimitedCrLf.line));

        setupInput("abcd\r\nnext\n");
        Terminal::Types::LineReadOptions normalizeLimitedOptions;
        normalizeLimitedOptions.maxBytes = 4;
        normalizeLimitedOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::NormalizeToLf;
        const Terminal::Types::LineReadResult normalizeLimited = Terminal::readLine(normalizeLimitedOptions);
        static_cast<void>(context.expectTrue("limited normalize status", normalizeLimited.status.ok()));
        static_cast<void>(context.expectEq("limited normalize text", std::string{"abcd"}, normalizeLimited.line));
        static_cast<void>(context.expectEq("limited normalize size", std::size_t{4}, normalizeLimited.line.size()));
        static_cast<void>(context.expectTrue("limited normalize reports truncation", normalizeLimited.wasTruncated));
        static_cast<void>(context.expectEq(
            "limited normalize ending consumed",
            Terminal::Types::ConsumedLineEnding::CrLf,
            normalizeLimited.consumedLineEnding));
        const Terminal::Types::LineReadResult afterLimitedNormalize = Terminal::readLine();
        static_cast<void>(context.expectTrue("after limited normalize status", afterLimitedNormalize.status.ok()));
        static_cast<void>(context.expectEq("after limited normalize text", std::string{"next"}, afterLimitedNormalize.line));

        setupInput("\xc3\xa9z");
        Terminal::Types::TextReadOptions textOptions;
        textOptions.maxBytes = 2;
        const Terminal::Types::TextReadResult utf8First = Terminal::readText(textOptions);
        static_cast<void>(context.expectTrue("UTF-8 text status", utf8First.status.ok()));
        static_cast<void>(context.expectEq("UTF-8 text preserves boundary", std::string{"\xc3\xa9"}, utf8First.text));
        static_cast<void>(context.expectTrue("UTF-8 text reports truncation", utf8First.wasTruncated));

        textOptions.maxBytes = 8;
        const Terminal::Types::TextReadResult utf8Second = Terminal::readText(textOptions);
        static_cast<void>(context.expectTrue("pending UTF-8 text status", utf8Second.status.ok()));
        static_cast<void>(context.expectEq("pending UTF-8 text", std::string{"z"}, utf8Second.text));

        setupInput("\xc3(");
        const Terminal::Types::TextReadResult invalidText = Terminal::readText(textOptions);
        static_cast<void>(context.expectEq("invalid UTF-8 text fails", ErrorCode::EncodingFailed, invalidText.status.code));

        setupInput("", false);
        const Terminal::Types::ByteReadResult wouldBlock = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
        static_cast<void>(context.expectTrue("would-block status", wouldBlock.status.ok()));
        static_cast<void>(context.expectEq("would-block outcome", Terminal::Types::ReadOutcome::WouldBlock, wouldBlock.outcome));

        Terminal::Types::TextReadOptions timeoutOptions;
        timeoutOptions.timeout = std::chrono::milliseconds{1};
        const Terminal::Types::TextReadResult timedOut = Terminal::readText(timeoutOptions);
        static_cast<void>(context.expectTrue("timed-out status", timedOut.status.ok()));
        static_cast<void>(context.expectEq("timed-out outcome", Terminal::Types::ReadOutcome::TimedOut, timedOut.outcome));

        Hooks::forceNextReadFailure(ErrorCode::PermissionDenied);
        const Terminal::Types::TextReadResult failedRead = Terminal::readText(textOptions);
        static_cast<void>(context.expectEq("forced read failure", ErrorCode::PermissionDenied, failedRead.status.code));

        Hooks::reset();
    }

    void testInputModes(TestSupport::Context &context)
    {
        Hooks::reset();
        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());

        const Terminal::Types::InputMode interactive = Terminal::makeInputMode(Terminal::Types::InputModePreset::InteractiveLine);
        const Terminal::Types::InputMode raw = Terminal::makeInputMode(Terminal::Types::InputModePreset::RawBytes);
        Hooks::setInputModeOverride(Terminal::Types::InputStream::Stdin, interactive);

        Terminal::Types::InputModeResult mode = Terminal::getInputMode();
        static_cast<void>(context.expectTrue("input mode status", mode.status.ok()));
        static_cast<void>(context.expectTrue("input mode starts interactive", mode.mode.lineBuffered));

        static_cast<void>(context.expectTrue("set raw mode succeeds", Terminal::setInputMode(raw).ok()));
        mode = Terminal::getInputMode();
        static_cast<void>(context.expectFalse("raw mode line buffering off", mode.mode.lineBuffered));
        static_cast<void>(context.expectFalse("raw mode echo off", mode.mode.echoInput));

        {
            Terminal::InputModeScope scope = Terminal::scopedInputMode(interactive);
            static_cast<void>(context.expectTrue("input mode scope active", scope.active()));
            mode = Terminal::getInputMode();
            static_cast<void>(context.expectTrue("scoped mode applied", mode.mode.lineBuffered));
            static_cast<void>(context.expectTrue("scope explicit restore succeeds", scope.restore().ok()));
        }

        mode = Terminal::getInputMode();
        static_cast<void>(context.expectFalse("scope restored previous raw mode", mode.mode.lineBuffered));

        static_cast<void>(context.expectTrue("restore default input mode succeeds", Terminal::restoreDefaultInputMode().ok()));
        mode = Terminal::getInputMode();
        static_cast<void>(context.expectTrue("default input mode restored", mode.mode.lineBuffered));

        Hooks::forceNextInputModeFailure(ErrorCode::NativeFailure);
        static_cast<void>(context.expectEq("forced input mode failure", ErrorCode::NativeFailure, Terminal::setInputMode(raw).code));

        Hooks::reset();
    }

    void testReaderWriterWrappers(TestSupport::Context &context)
    {
        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stderr);
        setupInput("wrapped\n");

        Terminal::Writer writer(Terminal::Types::OutputStream::Stderr);
        static_cast<void>(context.expectEq("writer default stream", Terminal::Types::OutputStream::Stderr, writer.defaultStream()));
        static_cast<void>(context.expectTrue("writer write succeeds", writer.writeText("err").ok()));
        static_cast<void>(context.expectEq(
            "writer writes to default stream",
            std::string{"err"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stderr)));

        Terminal::Reader reader;
        const Terminal::Types::LineReadResult line = reader.readLine();
        static_cast<void>(context.expectTrue("reader read status", line.status.ok()));
        static_cast<void>(context.expectEq("reader line", std::string{"wrapped"}, line.line));

        Hooks::reset();
    }
#endif

    void testHookDependentSuitesSkipped(TestSupport::Context &context)
    {
        context.skip("Terminal hook-dependent suites", "GAMEWIP_TERMINAL_TEST_HOOKS=0");
    }
} // namespace

namespace GameWIP::Test
{
    int runTerminalTests([[maybe_unused]] int argc, [[maybe_unused]] char **argv, const TerminalTestOptions &options)
    {
        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(
            std::format("Terminal test options: report={}", options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

        runner.runSuite("Terminal passive helpers", testPassiveHelpers);

#if GAMEWIP_TERMINAL_TEST_HOOKS
        runner.runSuite("Terminal capabilities and queries", testCapabilitiesAndQueries);
        runner.runSuite("Terminal text and style output", testTextAndStyleOutput);
        runner.runSuite("Terminal segmented and byte output", testSegmentedAndByteOutput);
        runner.runSuite("Terminal controls", testControls);
        runner.runSuite("Terminal input reads", testInputReads);
        runner.runSuite("Terminal input modes", testInputModes);
        runner.runSuite("Terminal Reader and Writer", testReaderWriterWrappers);
#else
        runner.runSuite("Terminal hook-dependent suites", testHookDependentSuitesSkipped);
#endif

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Terminal library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
