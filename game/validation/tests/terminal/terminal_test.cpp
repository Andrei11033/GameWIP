/// @file terminal_test.cpp
/// @brief Executable self-tests for the Terminal library.
///
/// The suite combines backend hooks, redirected and console-like endpoints,
/// reentrant child execution, state restoration, and opt-in manual tests.

#include "validation/tests/terminal/terminal_test.h"

#include "terminal/terminal.h"
#include "test_support/test_support.h"

#ifndef INTERNAL_TERMINAL_TEST_HOOKS
#define INTERNAL_TERMINAL_TEST_HOOKS 0
#endif

#if INTERNAL_TERMINAL_TEST_HOOKS
#include "terminal/internal/terminal_test_hooks.h"
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/// @brief Marker value whose formatter deliberately throws during output tests.
struct TerminalThrowingFormat
{
};

/// @brief Marker value whose formatter performs a nested write to the same Terminal stream.
struct TerminalReentrantFormat
{
};

template <> struct std::formatter<TerminalThrowingFormat>
{
    /// @brief Accepts the empty formatter specification used by the failure fixture.
    constexpr auto parse(std::format_parse_context &context)
    {
        return context.begin();
    }

    /// @brief Throws deliberately so Terminal formatting error conversion can be verified.
    template <typename FormatContext> auto format(const TerminalThrowingFormat &, FormatContext &context) const
    {
        throw std::format_error("terminal test formatter failure");
        return context.out();
    }
};

template <> struct std::formatter<TerminalReentrantFormat>
{
    /// @brief Accepts the empty formatter specification used by the reentry fixture.
    constexpr auto parse(std::format_parse_context &context)
    {
        return context.begin();
    }

    /// @brief Writes a nested record before producing the outer formatted value.
    template <typename FormatContext> auto format(const TerminalReentrantFormat &, FormatContext &context) const
    {
        const GameWIP::IO::Types::Status status = GameWIP::Terminal::writeText("inner");
        if (!status.ok())
        {
            throw std::runtime_error("terminal reentrant formatter write failed");
        }
        return std::format_to(context.out(), "outer");
    }
};

namespace
{
    namespace IO = GameWIP::IO;
    namespace Terminal = GameWIP::Terminal;
    namespace TestSupport = GameWIP::TestSupport;

    using ErrorCode = IO::Types::ErrorCode;
    using TerminalTestOptions = GameWIP::Test::TerminalTestOptions;

    inline constexpr std::string_view kReentrantFormatChildArgument = "--terminal-test-child=reentrant-format";

    static_assert(!std::is_aggregate_v<Terminal::Types::Color>);
    static_assert(!std::is_aggregate_v<Terminal::Types::WriteSegment>);

    template <typename Text>
    concept CanCreateTextSegment = requires(Text &&text) { Terminal::textSegment(std::forward<Text>(text)); };

    template <typename Text>
    concept CanCreateStyledTextSegment =
        requires(Text &&text, const Terminal::Types::TextStyle &style) { Terminal::styledTextSegment(std::forward<Text>(text), style); };

    template <typename Bytes>
    concept CanCreateByteSegment = requires(Bytes &&bytes) { Terminal::byteSegment(std::forward<Bytes>(bytes)); };

    static_assert(!CanCreateTextSegment<std::string>);
    static_assert(!CanCreateStyledTextSegment<std::string>);
    static_assert(!CanCreateByteSegment<std::vector<std::byte>>);

    /// @brief Exposes fixture text as bytes without copying or conversion.
    [[nodiscard]] std::span<const std::byte> bytesOf(std::string_view text)
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    /// @brief Copies fixture text into owning byte storage for result comparisons.
    [[nodiscard]] std::vector<std::byte> copyBytes(std::string_view text)
    {
        const std::span<const std::byte> bytes = bytesOf(text);
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    /// @brief Returns true when the process arguments contain one exact value.
    [[nodiscard]] bool hasArgument(int argc, char **argv, std::string_view expected)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]) == expected)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Exercises reentrant print and println calls in an isolated child process.
    [[nodiscard]] int runReentrantFormatChild()
    {
        if (!Terminal::print("{}", TerminalReentrantFormat{}).ok())
        {
            return 1;
        }

        Terminal::Types::LineWriteOptions options;
        options.lineEnding = Terminal::Types::LineEnding::Lf;
        return Terminal::println(options, "{}", TerminalReentrantFormat{}).ok() ? 0 : 1;
    }

    /// @brief Verifies formatter reentry without allowing a deadlock to hang the parent suite.
    void testReentrantFormatting(TestSupport::Context &context, std::string_view executablePath)
    {
        TestSupport::Types::ChildProcessOptions child;
        child.executablePath = std::filesystem::path(executablePath);
        child.arguments = {std::string(kReentrantFormatChildArgument)};
        child.timeout = std::chrono::milliseconds{5000};
        child.captureOutput = true;

        const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(child);
        static_cast<void>(context.expectTrue("reentrant formatter child infrastructure succeeds", result.status.ok()));
        static_cast<void>(context.expectEq("reentrant formatter child exits", TestSupport::Types::ChildProcessOutcome::Exited, result.outcome));
        static_cast<void>(context.expectEq("reentrant formatter child returns zero", std::uint32_t{0}, result.exitCode));
        static_cast<void>(
            context.expectEq("reentrant formatter preserves nested and outer output", std::string{"innerouterinnerouter\n"}, result.output));
    }

    /// @brief Records a human response as a test pass, failure, or skip.
    void recordManualAnswer(TestSupport::Context &context, std::string_view name, TestSupport::Types::ManualAnswer answer)
    {
        switch (answer)
        {
        case TestSupport::Types::ManualAnswer::Yes:
            context.pass(name);
            return;

        case TestSupport::Types::ManualAnswer::No:
            context.fail(name, "manual check rejected by user");
            return;

        case TestSupport::Types::ManualAnswer::Skipped:
            context.skip(name, "manual check skipped by user");
            return;
        }
    }

    /// @brief Prompts for and records one human-observed Terminal result.
    void recordManualCheck(TestSupport::Context &context, std::string_view name, std::string_view question)
    {
        recordManualAnswer(context, name, TestSupport::promptManualCheck(question));
    }

    /// @brief Reports one failed Terminal operation and returns whether it succeeded.
    [[nodiscard]] bool requireManualOperation(
        TestSupport::Context &context,
        std::string_view name,
        std::string_view operation,
        const IO::Types::Status &status)
    {
        if (status.ok())
        {
            return true;
        }

        std::string reason = std::format("{} failed with {}", operation, IO::errorCodeName(status.code));
        if (!status.message.empty())
        {
            reason += std::format(": {}", status.message);
        }
        context.fail(name, reason);
        return false;
    }

    /// @brief Displays representative UTF-8 text for visual verification.
    void testManualUnicodeOutput(TestSupport::Context &context)
    {
        constexpr std::string_view sample = "Unicode: cafe\xCC\x81 | \xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC | "
                                            "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E | \xF0\x9F\x98\x80";
        if (!requireManualOperation(context, "manual Unicode output", "writeLine", Terminal::writeLine(sample)))
        {
            return;
        }

        recordManualCheck(
            context,
            "manual Unicode output",
            "Does the preceding Unicode line show accented text, Greek, Japanese, and an emoji without replacement characters?");
    }

    /// @brief Displays portable basic colors and verifies that they remain visually distinct.
    void testManualBasicColorOutput(TestSupport::Context &context, const Terminal::Types::StyleCapabilities &capabilities)
    {
        if (!capabilities.basicColor)
        {
            context.skip("manual basic-color output", "the terminal does not report basic-color support");
            return;
        }

        Terminal::Types::LineWriteOptions redOptions;
        redOptions.styleMode = Terminal::Types::StyleMode::Required;
        redOptions.style.foreground = Terminal::basicColor(Terminal::Types::BasicColor::BrightRed);

        Terminal::Types::LineWriteOptions greenOptions;
        greenOptions.styleMode = Terminal::Types::StyleMode::Required;
        greenOptions.style.foreground = Terminal::basicColor(Terminal::Types::BasicColor::BrightGreen);

        Terminal::Types::LineWriteOptions blueOptions;
        blueOptions.styleMode = Terminal::Types::StyleMode::Required;
        blueOptions.style.foreground = Terminal::basicColor(Terminal::Types::BasicColor::BrightBlue);

        if (!requireManualOperation(
                context,
                "manual basic-color output",
                "red basic-color writeLine",
                Terminal::writeLine("Basic color: bright red", redOptions)) ||
            !requireManualOperation(
                context,
                "manual basic-color output",
                "green basic-color writeLine",
                Terminal::writeLine("Basic color: bright green", greenOptions)) ||
            !requireManualOperation(
                context,
                "manual basic-color output",
                "blue basic-color writeLine",
                Terminal::writeLine("Basic color: bright blue", blueOptions)))
        {
            return;
        }

        recordManualCheck(
            context,
            "manual basic-color output",
            "Are the preceding basic-color lines visibly red, green, and blue according to the terminal palette?");
    }

    /// @brief Displays an RGB color when the terminal can honor exact RGB requests.
    void testManualRgbColorOutput(TestSupport::Context &context, const Terminal::Types::StyleCapabilities &capabilities)
    {
        if (!capabilities.rgbColor)
        {
            context.skip("manual RGB-color output", "the terminal does not report RGB-color support");
            return;
        }

        Terminal::Types::LineWriteOptions rgbOptions;
        rgbOptions.styleMode = Terminal::Types::StyleMode::Required;
        rgbOptions.style.foreground = Terminal::rgbColor(40, 210, 120);

        if (!requireManualOperation(
                context,
                "manual RGB-color output",
                "RGB styled writeLine",
                Terminal::writeLine("RGB color: red=40 green=210 blue=120", rgbOptions)))
        {
            return;
        }

        recordManualCheck(context, "manual RGB-color output", "Does the preceding RGB line appear as the requested vivid green color?");
    }

    /// @brief Displays each text attribute independently when the terminal advertises it.
    void testManualTextStyles(TestSupport::Context &context, const Terminal::Types::StyleCapabilities &capabilities)
    {
        bool wroteAttribute = false;
        const auto testAttribute = [&](std::string_view label, bool supported, bool Terminal::Types::TextStyle::*attribute) -> bool
        {
            const std::string name = std::format("manual {} text style", label);
            if (!supported)
            {
                context.skip(name, "the terminal does not report this text-style capability");
                return true;
            }

            Terminal::Types::LineWriteOptions options;
            options.styleMode = Terminal::Types::StyleMode::Required;
            options.style.*attribute = true;
            wroteAttribute = true;
            return requireManualOperation(
                context,
                name,
                std::format("{} styled writeLine", label),
                Terminal::writeLine(std::format("Text style: {}", label), options));
        };

        if (!testAttribute("bold", capabilities.bold, &Terminal::Types::TextStyle::bold) ||
            !testAttribute("dim", capabilities.dim, &Terminal::Types::TextStyle::dim) ||
            !testAttribute("italic", capabilities.italic, &Terminal::Types::TextStyle::italic) ||
            !testAttribute("underline", capabilities.underline, &Terminal::Types::TextStyle::underline) ||
            !testAttribute("inverse", capabilities.inverse, &Terminal::Types::TextStyle::inverse) ||
            !testAttribute("strikethrough", capabilities.strikethrough, &Terminal::Types::TextStyle::strikethrough))
        {
            return;
        }

        if (!wroteAttribute)
        {
            context.skip("manual text styles", "the terminal does not report any text-style capability");
            return;
        }

        recordManualCheck(context, "manual text styles", "Did each displayed text-style line visibly match its label?");
    }

    /// @brief Verifies that a styled write does not leak state into the following plain write.
    void testManualStyleRestoration(TestSupport::Context &context, const Terminal::Types::StyleCapabilities &capabilities)
    {
        if (!capabilities.basicColor)
        {
            context.skip("manual style restoration", "the terminal does not report basic-color support");
            return;
        }

        Terminal::Types::LineWriteOptions styledOptions;
        styledOptions.styleMode = Terminal::Types::StyleMode::Required;
        styledOptions.style.foreground = Terminal::basicColor(Terminal::Types::BasicColor::BrightMagenta);
        styledOptions.style.bold = capabilities.bold;
        styledOptions.style.underline = capabilities.underline;

        if (!requireManualOperation(
                context,
                "manual style restoration",
                "styled writeLine",
                Terminal::writeLine("Style restoration: styled magenta line", styledOptions)) ||
            !requireManualOperation(
                context,
                "manual style restoration",
                "plain writeLine",
                Terminal::writeLine("Style restoration: terminal defaults restored")))
        {
            return;
        }

        recordManualCheck(
            context,
            "manual style restoration",
            "Is only the first restoration line styled, with the second line using the terminal defaults?");
    }

    /// @brief Verifies visible cursor save/restore behavior without changing the final cursor position.
    void testManualCursorBehavior(TestSupport::Context &context, const Terminal::Types::OutputCapabilities &capabilities)
    {
        if (!capabilities.supportsCursorSaveRestore)
        {
            context.skip("manual cursor behavior", "the terminal does not report cursor save/restore support");
            return;
        }

        if (!requireManualOperation(context, "manual cursor behavior", "saveCursorPosition", Terminal::saveCursorPosition()) ||
            !requireManualOperation(
                context,
                "manual cursor behavior",
                "placeholder writeText",
                Terminal::writeText("Cursor: this placeholder should be replaced                 ")) ||
            !requireManualOperation(context, "manual cursor behavior", "restoreCursorPosition", Terminal::restoreCursorPosition()) ||
            !requireManualOperation(
                context,
                "manual cursor behavior",
                "replacement writeLine",
                Terminal::writeLine("Cursor: PASS - saved position restored                     ")))
        {
            return;
        }

        recordManualCheck(context, "manual cursor behavior", "Is there one Cursor: PASS line above, with no visible placeholder line?");
    }

    /// @brief Enters an alternate screen, checks its contents, and restores the main screen.
    void testManualAlternateScreen(TestSupport::Context &context, const Terminal::Types::OutputCapabilities &capabilities)
    {
        if (!capabilities.supportsAlternateScreen || !capabilities.supportsClear)
        {
            context.skip("manual alternate screen", "the terminal does not report alternate-screen and clear support");
            return;
        }

        Terminal::AlternateScreenScope alternateScreen = Terminal::scopedAlternateScreen();
        if (!requireManualOperation(context, "manual alternate screen", "enter alternate screen", alternateScreen.status()))
        {
            return;
        }
        if (!alternateScreen.active())
        {
            context.fail("manual alternate screen", "alternate-screen scope did not become active");
            return;
        }

        TestSupport::Types::ManualAnswer answer = TestSupport::Types::ManualAnswer::Skipped;
        const bool contentReady = requireManualOperation(context, "manual alternate screen", "clear alternate screen", Terminal::clear()) &&
                                  requireManualOperation(
                                      context,
                                      "manual alternate screen",
                                      "alternate-screen writeLine",
                                      Terminal::writeLine("GameWIP Terminal alternate-screen check"));
        if (contentReady)
        {
            answer = TestSupport::promptManualCheck("Is this prompt displayed on a clean alternate screen?");
        }

        const bool restored = requireManualOperation(context, "manual alternate screen", "leave alternate screen", alternateScreen.leave());
        if (contentReady && restored)
        {
            recordManualAnswer(context, "manual alternate screen", answer);
        }
    }

    /// @brief Reads one exact line from the real terminal input stream.
    void testManualInput(TestSupport::Context &context)
    {
        constexpr std::string_view expected = "GameWIP-11";
        if (!requireManualOperation(
                context,
                "manual terminal input",
                "input prompt writeText",
                Terminal::writeText("Input: type GameWIP-11 and press Enter: ")))
        {
            return;
        }

        const Terminal::Types::LineReadResult result = Terminal::readLine();
        if (!requireManualOperation(context, "manual terminal input", "readLine", result.status))
        {
            return;
        }
        if (result.outcome != Terminal::Types::ReadOutcome::Completed)
        {
            context.fail("manual terminal input", "readLine did not complete normally");
            return;
        }
        if (result.line != expected)
        {
            context.fail("manual terminal input", std::format("expected '{}', received '{}'", expected, result.line));
            return;
        }

        context.pass("manual terminal input");
    }

    /// @brief Verifies that scoped input mode and cursor visibility changes restore terminal state.
    void testManualStateRestoration(
        TestSupport::Context &context,
        const Terminal::Types::InputCapabilities &inputCapabilities,
        const Terminal::Types::OutputCapabilities &outputCapabilities)
    {
        if (!inputCapabilities.supportsInputMode || !inputCapabilities.supportsEchoControl)
        {
            context.skip("manual input-mode restoration", "the terminal does not report input-mode and echo control support");
        }
        else
        {
            const Terminal::Types::InputModeResult originalMode = Terminal::getInputMode();
            if (requireManualOperation(context, "manual input-mode restoration", "get original input mode", originalMode.status))
            {
                Terminal::Types::InputMode hiddenInputMode = originalMode.mode;
                hiddenInputMode.lineBuffered = true;
                hiddenInputMode.echoInput = false;
                hiddenInputMode.processControlKeys = true;

                Terminal::InputModeScope inputMode = Terminal::scopedInputMode(hiddenInputMode);
                if (!requireManualOperation(context, "manual input-mode restoration", "disable input echo", inputMode.status()))
                {
                    return;
                }
                if (!inputMode.active())
                {
                    context.fail("manual input-mode restoration", "input-mode scope did not become active");
                    return;
                }

                if (!requireManualOperation(
                        context,
                        "manual input-mode restoration",
                        "hidden-input prompt writeText",
                        Terminal::writeText("State restoration: type hidden and press Enter (the word should not echo): ")))
                {
                    return;
                }
                const Terminal::Types::LineReadResult hiddenInput = Terminal::readLine();
                bool readSucceeded = requireManualOperation(context, "manual input-mode restoration", "read hidden input", hiddenInput.status);
                if (readSucceeded && (hiddenInput.outcome != Terminal::Types::ReadOutcome::Completed || hiddenInput.line != "hidden"))
                {
                    context.fail("manual input-mode restoration", "hidden input did not produce the requested line");
                    readSucceeded = false;
                }
                const bool restoreSucceeded =
                    requireManualOperation(context, "manual input-mode restoration", "restore input mode", inputMode.restore());
                const Terminal::Types::InputModeResult restoredMode = Terminal::getInputMode();
                const bool querySucceeded =
                    requireManualOperation(context, "manual input-mode restoration", "get restored input mode", restoredMode.status);

                if (readSucceeded && restoreSucceeded && querySucceeded)
                {
                    const bool modesMatch = restoredMode.mode.lineBuffered == originalMode.mode.lineBuffered &&
                                            restoredMode.mode.echoInput == originalMode.mode.echoInput &&
                                            restoredMode.mode.processControlKeys == originalMode.mode.processControlKeys;
                    if (!modesMatch)
                    {
                        context.fail("manual input-mode restoration", "the input mode did not match its original state after restoration");
                    }
                    else
                    {
                        recordManualCheck(
                            context,
                            "manual input-mode restoration",
                            "Was the word 'hidden' suppressed while typing, and is normal input echo restored now?");
                    }
                }
            }
        }

        if (!outputCapabilities.supportsCursorVisibility)
        {
            context.skip("manual cursor-visibility restoration", "the terminal does not report cursor visibility support");
            return;
        }

        Terminal::CursorHiddenScope hiddenCursor = Terminal::scopedCursorHidden();
        if (!requireManualOperation(context, "manual cursor-visibility restoration", "hide cursor", hiddenCursor.status()))
        {
            return;
        }
        if (!hiddenCursor.active())
        {
            context.fail("manual cursor-visibility restoration", "cursor-hidden scope did not become active");
            return;
        }

        const TestSupport::Types::ManualAnswer hiddenAnswer = TestSupport::promptManualCheck("Is the terminal cursor currently hidden?");
        if (!requireManualOperation(context, "manual cursor-visibility restoration", "restore cursor visibility", hiddenCursor.restore()))
        {
            return;
        }
        recordManualAnswer(context, "manual cursor hidden state", hiddenAnswer);
        recordManualCheck(
            context,
            "manual cursor-visibility restoration",
            "Is the cursor visible again with the main screen, default style, and normal input behavior intact?");
    }

    /// @brief Runs the opt-in human checks for Terminal UI behavior.
    void testManualUiChecks(TestSupport::Context &context, const TerminalTestOptions &options)
    {
        if (!options.enableManualTests)
        {
            context.skip("Terminal manual tests", "disabled by TerminalTestOptions");
            return;
        }

        const Terminal::Types::OutputCapabilitiesResult output = Terminal::prepareOutput();
        const Terminal::Types::InputCapabilitiesResult input = Terminal::getInputCapabilities();
        if (!requireManualOperation(context, "manual terminal capability setup", "prepare stdout", output.status) ||
            !requireManualOperation(context, "manual terminal capability setup", "query stdin", input.status))
        {
            return;
        }
        if (output.capabilities.kind != Terminal::Types::StreamKind::Terminal || input.capabilities.kind != Terminal::Types::StreamKind::Terminal)
        {
            context.skip("Terminal manual tests", "requires real terminal stdin and stdout");
            return;
        }

        context.manual("Terminal UI checks: verify each observation and answer yes, no, or skip when prompted.");
        testManualUnicodeOutput(context);
        testManualBasicColorOutput(context, output.capabilities.style);
        testManualRgbColorOutput(context, output.capabilities.style);
        testManualTextStyles(context, output.capabilities.style);
        testManualStyleRestoration(context, output.capabilities.style);
        testManualCursorBehavior(context, output.capabilities);
        testManualAlternateScreen(context, output.capabilities);
        testManualInput(context);
        testManualStateRestoration(context, input.capabilities, output.capabilities);
    }

    /// @brief Verifies passive enum validators, style helpers, and line-ending text.
    void testPassiveHelpers(TestSupport::Context &context)
    {
        const Terminal::Types::Color defaultColor = Terminal::defaultColor();
        static_cast<void>(context.expectEq("defaultColor kind", Terminal::Types::ColorKind::Default, defaultColor.kind()));

        const Terminal::Types::Color basic = Terminal::basicColor(Terminal::Types::BasicColor::BrightCyan);
        static_cast<void>(context.expectEq("basicColor kind", Terminal::Types::ColorKind::Basic, basic.kind()));
        static_cast<void>(context.expectEq("basicColor value", Terminal::Types::BasicColor::BrightCyan, basic.basic()));

        const Terminal::Types::Color invalidBasic = Terminal::basicColor(static_cast<Terminal::Types::BasicColor>(-1));
        static_cast<void>(context.expectEq("invalid basicColor falls back to default", Terminal::Types::ColorKind::Default, invalidBasic.kind()));

        const Terminal::Types::Color rgb = Terminal::rgbColor(1, 2, 3);
        static_cast<void>(context.expectEq("rgbColor kind", Terminal::Types::ColorKind::Rgb, rgb.kind()));
        static_cast<void>(context.expectEq("rgbColor red", std::uint8_t{1}, rgb.red()));
        static_cast<void>(context.expectEq("rgbColor green", std::uint8_t{2}, rgb.green()));
        static_cast<void>(context.expectEq("rgbColor blue", std::uint8_t{3}, rgb.blue()));

        const Terminal::Types::InputMode interactive = Terminal::makeInputMode(Terminal::Types::InputModePreset::InteractiveLine);
        static_cast<void>(context.expectTrue("interactive mode line buffered", interactive.lineBuffered));
        static_cast<void>(context.expectTrue("interactive mode echo", interactive.echoInput));
        static_cast<void>(context.expectTrue("interactive mode control keys", interactive.processControlKeys));

        const Terminal::Types::InputMode raw = Terminal::makeInputMode(Terminal::Types::InputModePreset::RawBytes);
        static_cast<void>(context.expectFalse("raw mode not line buffered", raw.lineBuffered));
        static_cast<void>(context.expectFalse("raw mode no echo", raw.echoInput));
        static_cast<void>(context.expectFalse("raw mode no control keys", raw.processControlKeys));

        const Terminal::Types::InputMode invalidMode = Terminal::makeInputMode(static_cast<Terminal::Types::InputModePreset>(-1));
        static_cast<void>(context.expectTrue("invalid mode falls back to line buffered", invalidMode.lineBuffered));
        static_cast<void>(context.expectTrue("invalid mode falls back to echo", invalidMode.echoInput));
        static_cast<void>(context.expectTrue("invalid mode falls back to control keys", invalidMode.processControlKeys));

        const Terminal::Types::WriteSegment text = Terminal::textSegment("text");
        static_cast<void>(context.expectEq("text segment kind", Terminal::Types::WriteSegmentKind::Text, text.kind()));
        static_cast<void>(context.expectEq("text segment view", std::string_view{"text"}, text.text()));

        Terminal::Types::TextStyle style;
        style.bold = true;
        const Terminal::Types::WriteSegment styled = Terminal::styledTextSegment("styled", style);
        static_cast<void>(context.expectEq("styled segment kind", Terminal::Types::WriteSegmentKind::StyledText, styled.kind()));
        static_cast<void>(context.expectTrue("styled segment stores style", styled.style().bold));

        const std::string byteText = "bytes";
        const Terminal::Types::WriteSegment bytes = Terminal::byteSegment(bytesOf(byteText));
        static_cast<void>(context.expectEq("byte segment kind", Terminal::Types::WriteSegmentKind::Bytes, bytes.kind()));
        static_cast<void>(context.expectEq("byte segment size", byteText.size(), bytes.bytes().size()));

        static_cast<void>(context.expectEq(
            "formatted print failure returns status",
            ErrorCode::InvalidArgument,
            Terminal::print("{}", TerminalThrowingFormat{}).code));
        Terminal::Types::LineWriteOptions lineOptions;
        lineOptions.lineEnding = Terminal::Types::LineEnding::Lf;
        static_cast<void>(context.expectEq(
            "formatted println failure returns status",
            ErrorCode::InvalidArgument,
            Terminal::println(lineOptions, "{}", TerminalThrowingFormat{}).code));
    }

#if INTERNAL_TERMINAL_TEST_HOOKS
    namespace Hooks = GameWIP::Terminal::TestHooks;

    /// @brief Returns a fixture that enables every style capability.
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

    /// @brief Returns prepared interactive-output capabilities for hook-backed tests.
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

    /// @brief Returns redirected-output capabilities without terminal controls.
    [[nodiscard]] Terminal::Types::OutputCapabilities redirectedOutputCapabilities() noexcept
    {
        return {.kind = Terminal::Types::StreamKind::Redirected, .supportsUtf8Text = true, .supportsByteOutput = true, .supportsFlush = true};
    }

    /// @brief Returns interactive-output capabilities before virtual-terminal preparation.
    [[nodiscard]] Terminal::Types::OutputCapabilities unpreparedTerminalOutputCapabilities() noexcept
    {
        return {
            .kind = Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteOutput = false,
            .supportsFlush = true,
            .supportsTerminalSize = true,
            .supportsCursorPositionQuery = true,
            .supportsBell = true};
    }

    /// @brief Returns interactive-input capabilities for hook-backed tests.
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

    /// @brief Resets hooks, installs output capabilities, and enables byte capture.
    void setupCapturedOutput(Terminal::Types::OutputStream stream, Terminal::Types::OutputCapabilities capabilities = terminalOutputCapabilities())
    {
        Hooks::setOutputCapabilitiesOverride(stream, capabilities);
        Hooks::setOutputCapture(stream, true);
        Hooks::clearCapturedOutput(stream);
    }

    /// @brief Resets hooks and installs deterministic stdin bytes and EOF policy.
    void setupInput(std::string_view bytes, bool endOfStreamWhenEmpty = true)
    {
        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, bytes, endOfStreamWhenEmpty);
    }

    /// @brief Verifies capability observation, preparation, size, position, and availability queries.
    void testCapabilitiesAndQueries(TestSupport::Context &context)
    {
        Hooks::reset();

        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
        const Terminal::Types::InputCapabilitiesResult inputCapabilities = Terminal::getInputCapabilities();
        static_cast<void>(context.expectTrue("input capabilities status", inputCapabilities.status.ok()));
        static_cast<void>(context.expectEq("input capability kind", Terminal::Types::StreamKind::Terminal, inputCapabilities.capabilities.kind));
        static_cast<void>(context.expectTrue("input capability mode support", inputCapabilities.capabilities.supportsInputMode));

        Hooks::forceNextInputCapabilityFailure(ErrorCode::PermissionDenied);
        static_cast<void>(
            context.expectEq("input capability forced failure", ErrorCode::PermissionDenied, Terminal::getInputCapabilities().status.code));

        const auto invalidInputStream = static_cast<Terminal::Types::InputStream>(99);
        static_cast<void>(context.expectEq(
            "invalid input capability stream is rejected",
            ErrorCode::InvalidArgument,
            Terminal::getInputCapabilities(invalidInputStream).status.code));
        static_cast<void>(context.expectEq(
            "invalid input read stream is rejected",
            ErrorCode::InvalidArgument,
            Terminal::readText(invalidInputStream).status.code));

        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
        const Terminal::Types::OutputCapabilitiesResult outputCapabilities = Terminal::getOutputCapabilities();
        static_cast<void>(context.expectTrue("output capabilities status", outputCapabilities.status.ok()));
        static_cast<void>(context.expectTrue("output style capability", outputCapabilities.capabilities.style.rgbColor));
        static_cast<void>(context.expectTrue("output cursor capability", outputCapabilities.capabilities.supportsCursorMovement));
        static_cast<void>(context.expectEq(
            "capability query does not prepare output",
            std::size_t{0},
            Hooks::outputPreparationCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectEq("output capability forced failure", ErrorCode::StatFailed, Terminal::getOutputCapabilities().status.code));

        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, unpreparedTerminalOutputCapabilities());
        Hooks::setPreparedOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, terminalOutputCapabilities());
        const Terminal::Types::OutputCapabilitiesResult prepared = Terminal::prepareOutput();
        static_cast<void>(context.expectTrue("explicit output preparation succeeds", prepared.status.ok()));
        static_cast<void>(context.expectTrue("explicit output preparation enables styling", prepared.capabilities.style.rgbColor));
        static_cast<void>(context.expectEq(
            "explicit output preparation count",
            std::size_t{1},
            Hooks::outputPreparationCallCount(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectTrue("prepared capabilities remain active", Terminal::getOutputCapabilities().capabilities.style.bold));
        static_cast<void>(context.expectEq(
            "query after preparation remains observational",
            std::size_t{1},
            Hooks::outputPreparationCallCount(Terminal::Types::OutputStream::Stdout)));
        const Terminal::Types::OutputCapabilitiesResult preparedAgain = Terminal::prepareOutput();
        static_cast<void>(context.expectTrue("repeated output preparation succeeds", preparedAgain.status.ok()));
        static_cast<void>(context.expectTrue("repeated output preparation preserves capabilities", preparedAgain.capabilities.style.bold));

        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stderr, redirectedOutputCapabilities());
        const Terminal::Types::OutputCapabilitiesResult redirectedPrepared = Terminal::prepareOutput(Terminal::Types::OutputStream::Stderr);
        static_cast<void>(context.expectTrue("redirected output preparation succeeds", redirectedPrepared.status.ok()));
        static_cast<void>(context.expectEq(
            "redirected output preparation preserves stream kind",
            Terminal::Types::StreamKind::Redirected,
            redirectedPrepared.capabilities.kind));

        Terminal::Types::OutputCapabilities detachedCapabilities;
        detachedCapabilities.kind = Terminal::Types::StreamKind::Detached;
        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stderr, detachedCapabilities);
        static_cast<void>(context.expectEq(
            "detached output preparation reports not open",
            ErrorCode::NotOpen,
            Terminal::prepareOutput(Terminal::Types::OutputStream::Stderr).status.code));

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

        const Terminal::Types::CursorPositionResult explicitPosition = Terminal::getCursorPosition(
            Terminal::Types::OutputStream::Stdout,
            Terminal::Types::InputStream::Stdin,
            {.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None});
        static_cast<void>(context.expectTrue("explicit cursor position status", explicitPosition.status.ok()));
        static_cast<void>(context.expectEq("explicit cursor position column", std::uint32_t{7}, explicitPosition.position.column));
        static_cast<void>(context.expectEq("explicit cursor position row", std::uint32_t{9}, explicitPosition.position.row));

        Hooks::forceNextTerminalSizeFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectEq("terminal size forced failure", ErrorCode::StatFailed, Terminal::getTerminalSize().status.code));

        Hooks::forceNextCursorPositionFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectEq("cursor position forced failure", ErrorCode::StatFailed, Terminal::getCursorPosition().status.code));

        Hooks::reset();
    }

    /// @brief Verifies plain, formatted, buffered, styled, and line-oriented text output.
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
        Terminal::Types::LineWriteOptions invalidLineEndingOptions;
        invalidLineEndingOptions.lineEnding = static_cast<Terminal::Types::LineEnding>(-1);
        static_cast<void>(context.expectEq(
            "invalid line ending is rejected before output",
            ErrorCode::InvalidArgument,
            Terminal::writeLine("must-not-write", invalidLineEndingOptions).code));
        static_cast<void>(
            context.expectTrue("invalid line ending writes nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        Terminal::Types::TextWriteOptions invalidFlushOptions;
        invalidFlushOptions.flushMode = static_cast<IO::Types::FlushMode>(-1);
        static_cast<void>(context.expectEq(
            "invalid text flush mode is rejected before output",
            ErrorCode::InvalidArgument,
            Terminal::writeText("must-not-write", invalidFlushOptions).code));
        static_cast<void>(
            context.expectTrue("invalid text flush mode writes nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));
        static_cast<void>(context.expectEq(
            "invalid direct flush mode is rejected",
            ErrorCode::InvalidArgument,
            Terminal::flush(static_cast<IO::Types::FlushMode>(-1)).code));

        bool invalidOutputBufferThrew = false;
        try
        {
            [[maybe_unused]] Terminal::OutputBuffer invalidBuffer(static_cast<Terminal::Types::LineEnding>(-1));
        }
        catch (const std::invalid_argument &)
        {
            invalidOutputBufferThrew = true;
        }
        static_cast<void>(context.expectTrue("output buffer rejects invalid line ending", invalidOutputBufferThrew));

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
        static_cast<void>(
            context.expectEq("styled write uses one backend call", std::size_t{2}, Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout, redirectedOutputCapabilities());
        static_cast<void>(
            context.expectTrue("redirected styled output falls back to plain text", Terminal::writeText("redirected", styledOptions).ok()));
        static_cast<void>(context.expectEq(
            "redirected styled output does not prepare",
            std::size_t{0},
            Hooks::outputPreparationCallCount(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "redirected styled output capture is plain",
            std::string{"redirected"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout, unpreparedTerminalOutputCapabilities());
        Hooks::setPreparedOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, terminalOutputCapabilities());
        static_cast<void>(context.expectTrue("lazy preparation enables styled output", Terminal::writeText("lazy", styledOptions).ok()));
        static_cast<void>(
            context.expectEq("lazy preparation count", std::size_t{1}, Hooks::outputPreparationCallCount(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "lazy styled output capture",
            std::string{"\x1b[1;91mlazy\x1b[0m"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, unpreparedTerminalOutputCapabilities());
        Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
        static_cast<void>(context.expectTrue("auto style fallback succeeds", Terminal::writeText("plain", styledOptions).ok()));
        static_cast<void>(
            context.expectEq("auto style fallback is plain", std::string{"plain"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        styledOptions.styleMode = Terminal::Types::StyleMode::Required;
        Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
        static_cast<void>(
            context.expectEq("forced preparation failure propagates", ErrorCode::NativeFailure, Terminal::writeText("fail", styledOptions).code));
        static_cast<void>(
            context.expectTrue("forced unsupported style writes nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("resetStyle succeeds", Terminal::resetStyle().ok()));
        static_cast<void>(
            context.expectEq("resetStyle emits reset", std::string{"\x1b[0m"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::forceNextTextWriteFailure(ErrorCode::PermissionDenied);
        static_cast<void>(context.expectEq("forced text write failure", ErrorCode::PermissionDenied, Terminal::writeText("blocked").code));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        const std::size_t printWritesBefore = Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("formatted print succeeds", Terminal::print("value {}", 42).ok()));
        static_cast<void>(
            context.expectEq("formatted print capture", std::string{"value 42"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "formatted print uses one backend call",
            printWritesBefore + 1,
            Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectEq(
            "formatted print failure returns status",
            ErrorCode::InvalidArgument,
            Terminal::print("{}", TerminalThrowingFormat{}).code));
        static_cast<void>(
            context.expectTrue("formatted print failure writes nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Types::LineWriteOptions printLineOptions;
        printLineOptions.lineEnding = Terminal::Types::LineEnding::Lf;
        const std::size_t printlnWritesBefore = Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("formatted println succeeds", Terminal::println(printLineOptions, "line {}", 7).ok()));
        static_cast<void>(
            context.expectEq("formatted println capture", std::string{"line 7\n"}, Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "formatted println uses one backend call",
            printlnWritesBefore + 1,
            Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectEq(
            "formatted println failure returns status",
            ErrorCode::InvalidArgument,
            Terminal::println(printLineOptions, "{}", TerminalThrowingFormat{}).code));
        static_cast<void>(
            context.expectTrue("formatted println failure writes nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::OutputBuffer outputBuffer(Terminal::Types::LineEnding::Lf);
        outputBuffer.reserve(64);
        outputBuffer.appendText("alpha");
        outputBuffer.appendLine(" beta");
        outputBuffer.print("{}", 3);
        outputBuffer.println(" {}", 4);
        static_cast<void>(context.expectEq("output buffer text", std::string_view{"alpha beta\n3 4\n"}, outputBuffer.text()));

        static_cast<void>(context.expectTrue("output buffer flush succeeds", outputBuffer.flushTo().ok()));
        static_cast<void>(context.expectTrue("output buffer clears after flush", outputBuffer.empty()));
        static_cast<void>(context.expectEq(
            "output buffer flush capture",
            std::string{"alpha beta\n3 4\n"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        outputBuffer.appendText("retry");
        Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
        static_cast<void>(context.expectEq("output buffer failure propagates", ErrorCode::WriteFailed, outputBuffer.flushTo().code));
        static_cast<void>(context.expectEq("output buffer failure preserves text", std::string_view{"retry"}, outputBuffer.text()));
        static_cast<void>(
            context.expectTrue("output buffer explicit stream retry succeeds", outputBuffer.flushTo(Terminal::Types::OutputStream::Stdout).ok()));
        static_cast<void>(context.expectTrue("output buffer explicit stream retry clears text", outputBuffer.empty()));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue(
            "explicit stream formatted print succeeds",
            Terminal::print(Terminal::Types::OutputStream::Stdout, "writer {}", 5).ok()));
        static_cast<void>(context.expectEq(
            "explicit stream formatted print capture",
            std::string{"writer 5"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue(
            "explicit stream formatted println succeeds",
            Terminal::println(Terminal::Types::OutputStream::Stdout, printLineOptions, "writer line {}", 6).ok()));
        static_cast<void>(context.expectEq(
            "explicit stream formatted println capture",
            std::string{"writer line 6\n"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout, redirectedOutputCapabilities());
        setupCapturedOutput(Terminal::Types::OutputStream::Stderr, redirectedOutputCapabilities());
        static_cast<void>(context.expectTrue("stdout state write succeeds", Terminal::writeText("out").ok()));
        static_cast<void>(context.expectTrue("stderr state write succeeds", Terminal::writeText(Terminal::Types::OutputStream::Stderr, "err").ok()));
        static_cast<void>(context.expectEq(
            "stdout state remains independent",
            std::string{"out"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "stderr state remains independent",
            std::string{"err"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stderr)));

        Hooks::reset();
    }

    /// @brief Verifies atomic segmented records, raw bytes, output capture, and flush behavior.
    void testSegmentedAndByteOutput(TestSupport::Context &context)
    {
        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);

        Terminal::Types::TextStyle style;
        style.bold = true;
        const std::string byteText = "c";
        const std::array<Terminal::Types::WriteSegment, 3> segments{
            Terminal::textSegment("a"),
            Terminal::styledTextSegment("b", style),
            Terminal::byteSegment(bytesOf(byteText))};

        Terminal::Types::SegmentWriteOptions options;
        options.appendLineEnding = true;
        options.lineEnding = Terminal::Types::LineEnding::Lf;
        static_cast<void>(context.expectTrue(
            "segmented write succeeds",
            Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(segments), options).ok()));
        static_cast<void>(context.expectEq(
            "segmented write preserves order",
            std::string{"a\x1b[1mb\x1b[0mc\n"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "segmented write uses one backend call",
            std::size_t{1},
            Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        const std::array<Terminal::Types::WriteSegment, 1> plainSegments{Terminal::textSegment("plain")};
        const std::size_t plainSegmentWritesBefore = Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout);
        Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
        static_cast<void>(context.expectTrue(
            "plain segmented write skips capability query",
            Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(plainSegments)).ok()));
        static_cast<void>(context.expectEq(
            "plain segmented write capture",
            std::string{"plain"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq(
            "plain segmented write leaves capability failure pending",
            ErrorCode::StatFailed,
            Terminal::getOutputCapabilities().status.code));

        static_cast<void>(context.expectEq(
            "single plain segment uses one direct backend write",
            plainSegmentWritesBefore + 1,
            Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Types::SegmentWriteOptions invalidLineEndingOptions;
        invalidLineEndingOptions.appendLineEnding = true;
        invalidLineEndingOptions.lineEnding = static_cast<Terminal::Types::LineEnding>(-1);
        static_cast<void>(context.expectEq(
            "invalid segmented line ending is rejected before output",
            ErrorCode::InvalidArgument,
            Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(plainSegments), invalidLineEndingOptions).code));
        static_cast<void>(context.expectTrue(
            "invalid segmented line ending writes nothing",
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
        const std::array<Terminal::Types::WriteSegment, 1> styledSegments{Terminal::styledTextSegment("fallback", style)};
        Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
        Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
        static_cast<void>(context.expectTrue(
            "auto styled segment falls back after capability and preparation failure",
            Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(styledSegments)).ok()));
        static_cast<void>(context.expectEq(
            "auto styled segment fallback is plain",
            std::string{"fallback"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Types::SegmentWriteOptions requiredOptions;
        requiredOptions.styleMode = Terminal::Types::StyleMode::Required;
        Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
        Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
        static_cast<void>(context.expectEq(
            "required styled segment propagates preparation failure",
            ErrorCode::NativeFailure,
            Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(styledSegments), requiredOptions).code));
        static_cast<void>(context.expectTrue(
            "required styled segment failure writes nothing",
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout, unpreparedTerminalOutputCapabilities());
        const std::array<Terminal::Types::WriteSegment, 2> unsupportedSegments{
            Terminal::textSegment("must-not-write"),
            Terminal::byteSegment(bytesOf(byteText))};
        static_cast<void>(context.expectEq(
            "unsupported byte segment rejects full batch",
            ErrorCode::Unsupported,
            Terminal::writeSegments(std::span<const Terminal::Types::WriteSegment>(unsupportedSegments)).code));
        static_cast<void>(
            context.expectTrue("unsupported segment batch emits nothing", Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));
        static_cast<void>(context.expectEq(
            "unsupported segment batch makes no write",
            std::size_t{0},
            Hooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout)));

        Hooks::reset();
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout, redirectedOutputCapabilities());
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

    /// @brief Verifies cursor, clear, scroll, title, alternate-screen, and visibility controls.
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
        {
            Terminal::CursorHiddenScope outer = Terminal::scopedCursorHidden();
            static_cast<void>(context.expectTrue("outer cursor scope active", outer.active()));
            {
                Terminal::CursorHiddenScope inner = Terminal::scopedCursorHidden();
                static_cast<void>(context.expectTrue("inner cursor scope active", inner.active()));
                static_cast<void>(context.expectTrue("inner cursor restore succeeds", inner.restore().ok()));
                static_cast<void>(context.expectEq(
                    "inner cursor restore keeps cursor hidden",
                    std::string{"\x1b[?25l"},
                    Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
            }
            static_cast<void>(context.expectTrue("outer cursor restore succeeds", outer.restore().ok()));
        }
        static_cast<void>(context.expectEq(
            "nested cursor scopes emit one hide and show",
            std::string{"\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::CursorHiddenScope scope = Terminal::scopedCursorHidden();
            Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
            static_cast<void>(context.expectEq("cursor restore failure propagates", ErrorCode::WriteFailed, scope.restore().code));
            static_cast<void>(context.expectTrue("failed cursor restore remains active", scope.active()));
            static_cast<void>(context.expectTrue("cursor restore retry succeeds", scope.restore().ok()));
            static_cast<void>(context.expectFalse("successful cursor restore becomes inactive", scope.active()));
        }

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::AlternateScreenScope outer = Terminal::scopedAlternateScreen();
            static_cast<void>(context.expectTrue("outer alternate-screen scope active", outer.active()));
            {
                Terminal::AlternateScreenScope inner = Terminal::scopedAlternateScreen();
                static_cast<void>(context.expectTrue("inner alternate-screen scope active", inner.active()));
                static_cast<void>(context.expectTrue("inner alternate-screen leave succeeds", inner.leave().ok()));
                static_cast<void>(context.expectEq(
                    "inner alternate-screen leave keeps mode active",
                    std::string{"\x1b[?1049h"},
                    Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
            }
            static_cast<void>(context.expectTrue("outer alternate-screen leave succeeds", outer.leave().ok()));
        }
        static_cast<void>(context.expectEq(
            "nested alternate-screen scopes emit one enter and leave",
            std::string{"\x1b[?1049h\x1b[?1049l"},
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

        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, unpreparedTerminalOutputCapabilities());
        Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
        static_cast<void>(context.expectEq(
            "control preparation failure propagates",
            ErrorCode::NativeFailure,
            Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 1).code));
        static_cast<void>(context.expectTrue(
            "control preparation failure writes nothing",
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout).empty()));

        Hooks::setOutputCapabilitiesOverride(Terminal::Types::OutputStream::Stdout, redirectedOutputCapabilities());
        static_cast<void>(context.expectTrue("zero move is no-op success", Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 0).ok()));
        static_cast<void>(context.expectEq(
            "zero move still validates direction",
            ErrorCode::InvalidArgument,
            Terminal::moveCursor(static_cast<Terminal::Types::CursorMoveDirection>(-1), 0).code));
        static_cast<void>(context.expectEq(
            "zero scroll still validates direction",
            ErrorCode::InvalidArgument,
            Terminal::scroll(static_cast<Terminal::Types::ScrollDirection>(-1), 0).code));
        static_cast<void>(context.expectEq(
            "cursor movement rejects values beyond VT limit",
            ErrorCode::InvalidArgument,
            Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 32768).code));
        static_cast<void>(context.expectEq(
            "cursor position rejects values beyond VT limit",
            ErrorCode::InvalidArgument,
            Terminal::setCursorPosition({.column = 32767, .row = 0}).code));
        static_cast<void>(context.expectEq(
            "scroll rejects values beyond VT limit",
            ErrorCode::InvalidArgument,
            Terminal::scroll(Terminal::Types::ScrollDirection::Down, 32768).code));

        const auto invalidOutputStream = static_cast<Terminal::Types::OutputStream>(99);
        static_cast<void>(context.expectEq(
            "invalid output stream is rejected",
            ErrorCode::InvalidArgument,
            Terminal::writeText(invalidOutputStream, "must-not-write").code));
        static_cast<void>(context.expectEq(
            "invalid output capability stream is rejected",
            ErrorCode::InvalidArgument,
            Terminal::getOutputCapabilities(invalidOutputStream).status.code));

        const std::string oversizedTitle(255, 'x');
        static_cast<void>(
            context.expectEq("title rejects values beyond VT limit", ErrorCode::InvalidArgument, Terminal::setTitle(oversizedTitle).code));

        Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
        static_cast<void>(context.expectEq(
            "forced flush failure through control",
            ErrorCode::FlushFailed,
            Terminal::moveCursor(Terminal::Types::CursorMoveDirection::Down, 0, {.flushMode = IO::Types::FlushMode::Data}).code));

        Hooks::reset();
    }

    /// @brief Verifies byte, text, and line reads across UTF-8, timeout, truncation, and EOF paths.
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

        setupInput("kept\n");
        Terminal::Types::LineReadOptions invalidLineModeOptions;
        invalidLineModeOptions.lineEndingMode = static_cast<Terminal::Types::ReadLineEndingMode>(-1);
        static_cast<void>(context.expectEq(
            "invalid line read mode is rejected",
            ErrorCode::InvalidArgument,
            Terminal::readLine(invalidLineModeOptions).status.code));
        static_cast<void>(context.expectEq("invalid line read mode consumes no input", std::string{"kept"}, Terminal::readLine().line));

        setupInput("line\n");
        Terminal::Types::LineReadOptions zeroLineLimitOptions;
        zeroLineLimitOptions.maxReturnedBytes = 0;
        static_cast<void>(
            context.expectEq("zero line limit is rejected", ErrorCode::InvalidArgument, Terminal::readLine(zeroLineLimitOptions).status.code));
        static_cast<void>(context.expectEq("zero line limit consumes no input", std::string{"line"}, Terminal::readLine().line));

        setupInput("text");
        Terminal::Types::TextReadOptions zeroTextLimitOptions;
        zeroTextLimitOptions.maxReturnedBytes = 0;
        static_cast<void>(
            context.expectEq("zero text limit is rejected", ErrorCode::InvalidArgument, Terminal::readText(zeroTextLimitOptions).status.code));
        static_cast<void>(context.expectEq("zero text limit consumes no input", std::string{"text"}, Terminal::readText().text));

        setupInput("abc\nnext\n");
        Terminal::Types::LineReadOptions keepLimitedLfOptions;
        keepLimitedLfOptions.maxReturnedBytes = 3;
        keepLimitedLfOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::Keep;
        const Terminal::Types::LineReadResult keepLimitedLf = Terminal::readLine(keepLimitedLfOptions);
        static_cast<void>(context.expectTrue("limited LF keep status", keepLimitedLf.status.ok()));
        static_cast<void>(context.expectEq("limited LF keep text", std::string{"abc"}, keepLimitedLf.line));
        static_cast<void>(context.expectEq("limited LF keep size", std::size_t{3}, keepLimitedLf.line.size()));
        static_cast<void>(context.expectTrue("limited LF keep reports truncation", keepLimitedLf.wasTruncated));
        static_cast<void>(
            context.expectEq("limited LF keep ending consumed", Terminal::Types::ConsumedLineEnding::Lf, keepLimitedLf.consumedLineEnding));
        const Terminal::Types::LineReadResult afterLimitedLf = Terminal::readLine();
        static_cast<void>(context.expectTrue("after limited LF status", afterLimitedLf.status.ok()));
        static_cast<void>(context.expectEq("after limited LF text", std::string{"next"}, afterLimitedLf.line));

        setupInput("abc\r\nnext\n");
        Terminal::Types::LineReadOptions keepLimitedCrLfOptions;
        keepLimitedCrLfOptions.maxReturnedBytes = 4;
        keepLimitedCrLfOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::Keep;
        const Terminal::Types::LineReadResult keepLimitedCrLf = Terminal::readLine(keepLimitedCrLfOptions);
        static_cast<void>(context.expectTrue("limited CRLF keep status", keepLimitedCrLf.status.ok()));
        static_cast<void>(context.expectEq("limited CRLF keep text avoids partial ending", std::string{"abc"}, keepLimitedCrLf.line));
        static_cast<void>(context.expectTrue("limited CRLF keep reports truncation", keepLimitedCrLf.wasTruncated));
        static_cast<void>(
            context.expectEq("limited CRLF keep ending consumed", Terminal::Types::ConsumedLineEnding::CrLf, keepLimitedCrLf.consumedLineEnding));
        const Terminal::Types::LineReadResult afterLimitedCrLf = Terminal::readLine();
        static_cast<void>(context.expectTrue("after limited CRLF status", afterLimitedCrLf.status.ok()));
        static_cast<void>(context.expectEq("after limited CRLF text", std::string{"next"}, afterLimitedCrLf.line));

        setupInput("abcd\r\nnext\n");
        Terminal::Types::LineReadOptions normalizeLimitedOptions;
        normalizeLimitedOptions.maxReturnedBytes = 4;
        normalizeLimitedOptions.lineEndingMode = Terminal::Types::ReadLineEndingMode::NormalizeToLf;
        const Terminal::Types::LineReadResult normalizeLimited = Terminal::readLine(normalizeLimitedOptions);
        static_cast<void>(context.expectTrue("limited normalize status", normalizeLimited.status.ok()));
        static_cast<void>(context.expectEq("limited normalize text", std::string{"abcd"}, normalizeLimited.line));
        static_cast<void>(context.expectEq("limited normalize size", std::size_t{4}, normalizeLimited.line.size()));
        static_cast<void>(context.expectTrue("limited normalize reports truncation", normalizeLimited.wasTruncated));
        static_cast<void>(
            context.expectEq("limited normalize ending consumed", Terminal::Types::ConsumedLineEnding::CrLf, normalizeLimited.consumedLineEnding));
        const Terminal::Types::LineReadResult afterLimitedNormalize = Terminal::readLine();
        static_cast<void>(context.expectTrue("after limited normalize status", afterLimitedNormalize.status.ok()));
        static_cast<void>(context.expectEq("after limited normalize text", std::string{"next"}, afterLimitedNormalize.line));

        std::string longCrLfLine(4095, 'x');
        longCrLfLine.append("\r\nnext\n");
        setupInput(longCrLfLine);
        const Terminal::Types::LineReadResult longLine = Terminal::readLine();
        static_cast<void>(context.expectTrue("long line status", longLine.status.ok()));
        static_cast<void>(context.expectEq("long line size", std::size_t{4095}, longLine.line.size()));
        static_cast<void>(
            context.expectEq("long line detects CRLF across read chunks", Terminal::Types::ConsumedLineEnding::CrLf, longLine.consumedLineEnding));
        static_cast<void>(context.expectEq("line after chunk-boundary CRLF", std::string{"next"}, Terminal::readLine().line));

        setupInput("\xc3\xa9z");
        Terminal::Types::TextReadOptions textOptions;
        textOptions.maxReturnedBytes = 2;
        const Terminal::Types::TextReadResult utf8First = Terminal::readText(textOptions);
        static_cast<void>(context.expectTrue("UTF-8 text status", utf8First.status.ok()));
        static_cast<void>(context.expectEq("UTF-8 text preserves boundary", std::string{"\xc3\xa9"}, utf8First.text));
        static_cast<void>(context.expectTrue("UTF-8 text reports truncation", utf8First.wasTruncated));

        textOptions.maxReturnedBytes = 8;
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

#if defined(_WIN32)
    /// @brief Verifies buffered bytes are owned by native stdin identity, including numeric handle reuse.
    void testInputEndpointReplacement(TestSupport::Context &context)
    {
        Hooks::reset();
        const HANDLE originalInput = GetStdHandle(STD_INPUT_HANDLE);

        HANDLE firstRead = nullptr;
        HANDLE firstWrite = nullptr;
        static_cast<void>(context.expectTrue("create first stdin pipe", CreatePipe(&firstRead, &firstWrite, nullptr, 0) != FALSE));
        if (firstRead == nullptr || firstWrite == nullptr)
        {
            return;
        }

        static_cast<void>(context.expectTrue("install first stdin pipe", SetStdHandle(STD_INPUT_HANDLE, firstRead) != FALSE));
        DWORD bytesWritten = 0;
        static_cast<void>(context.expectTrue("write first stdin pipe", WriteFile(firstWrite, "AB", 2, &bytesWritten, nullptr) != FALSE));
        CloseHandle(firstWrite);

        Terminal::Types::TextReadOptions oneByte;
        oneByte.maxReturnedBytes = 1;
        static_cast<void>(context.expectEq("first endpoint returns first byte", std::string{"A"}, Terminal::readText(oneByte).text));
        Hooks::setPendingHighSurrogate(Terminal::Types::InputStream::Stdin, UINT16_C(0xD83D));
        static_cast<void>(
            context.expectTrue("first endpoint retains seeded high surrogate", Hooks::hasPendingHighSurrogate(Terminal::Types::InputStream::Stdin)));

        const HANDLE reusedValue = firstRead;
        CloseHandle(firstRead);

        HANDLE replacementRead = nullptr;
        HANDLE replacementWrite = nullptr;
        for (std::size_t attempt = 0; attempt < 256; ++attempt)
        {
            HANDLE candidateRead = nullptr;
            HANDLE candidateWrite = nullptr;
            if (CreatePipe(&candidateRead, &candidateWrite, nullptr, 0) == FALSE)
            {
                break;
            }
            if (candidateRead == reusedValue)
            {
                replacementRead = candidateRead;
                replacementWrite = candidateWrite;
                break;
            }
            CloseHandle(candidateRead);
            CloseHandle(candidateWrite);
        }

        if (replacementRead == nullptr)
        {
            context.skip("stdin numeric handle reuse", "Win32 did not reuse the released handle value within 256 attempts");
            static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
            Hooks::reset();
            return;
        }

        static_cast<void>(context.expectTrue("install reused stdin handle", SetStdHandle(STD_INPUT_HANDLE, replacementRead) != FALSE));
        bytesWritten = 0;
        static_cast<void>(context.expectTrue("write replacement stdin pipe", WriteFile(replacementWrite, "C", 1, &bytesWritten, nullptr) != FALSE));
        CloseHandle(replacementWrite);

        const Terminal::Types::TextReadResult replacement = Terminal::readText(oneByte);
        static_cast<void>(context.expectTrue("replacement endpoint read succeeds", replacement.status.ok()));
        static_cast<void>(context.expectEq("replacement endpoint discards stale byte", std::string{"C"}, replacement.text));
        static_cast<void>(context.expectFalse(
            "replacement endpoint discards stale high surrogate",
            Hooks::hasPendingHighSurrogate(Terminal::Types::InputStream::Stdin)));

        static_cast<void>(context.expectTrue("detach stdin", SetStdHandle(STD_INPUT_HANDLE, nullptr) != FALSE));
        static_cast<void>(
            context.expectEq("detached stdin reports NotOpen after state reset", ErrorCode::NotOpen, Terminal::getInputAvailability().status.code));

        static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
        CloseHandle(replacementRead);
        Hooks::reset();
    }
#endif

    /// @brief Verifies mode queries, updates, default restore, and scoped exact restoration.
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

        Terminal::Types::InputMode invalidEchoMode = raw;
        invalidEchoMode.echoInput = true;
        static_cast<void>(context.expectEq(
            "Win32 input mode rejects echo without line buffering",
            ErrorCode::InvalidArgument,
            Terminal::setInputMode(invalidEchoMode).code));
        mode = Terminal::getInputMode();
        static_cast<void>(context.expectFalse("invalid input mode leaves current mode unchanged", mode.mode.lineBuffered));
        static_cast<void>(context.expectFalse("invalid input mode leaves echo unchanged", mode.mode.echoInput));

        setupInput("\xc3\xa9z");
        Terminal::Types::TextReadOptions splitTextOptions;
        splitTextOptions.maxReturnedBytes = 2;
        static_cast<void>(context.expectEq(
            "mode preservation setup reads one UTF-8 code point",
            std::string{"\xc3\xa9"},
            Terminal::readText(splitTextOptions).text));
        static_cast<void>(context.expectTrue("mode change with pending input succeeds", Terminal::setInputMode(interactive).ok()));
        splitTextOptions.maxReturnedBytes = 8;
        static_cast<void>(context.expectEq("mode change preserves pending input", std::string{"z"}, Terminal::readText(splitTextOptions).text));
        static_cast<void>(context.expectTrue("raw mode restored after pending-input check", Terminal::setInputMode(raw).ok()));

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

#endif

    /// @brief Records a clear skip when Terminal test hooks were not compiled.
    void testHookDependentSuitesSkipped(TestSupport::Context &context)
    {
        context.skip("Terminal hook-dependent suites", "INTERNAL_TERMINAL_TEST_HOOKS=0");
    }
} // namespace

namespace GameWIP::Test
{
    int runTerminalTests(int argc, char **argv, const TerminalTestOptions &options)
    {
        if (hasArgument(argc, argv, kReentrantFormatChildArgument))
        {
            return runReentrantFormatChild();
        }

        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(
            std::format("Terminal test options: report={}", options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

        runner.runSuite("Terminal passive helpers", testPassiveHelpers);
        runner.runSuite(
            "Terminal reentrant formatting",
            [&](TestSupport::Context &context)
            {
                testReentrantFormatting(context, argc > 0 && argv[0] != nullptr ? argv[0] : "");
            });

        runner.runSuite(
            "Terminal manual tests",
            [&options](TestSupport::Context &context)
            {
                testManualUiChecks(context, options);
            });

#if INTERNAL_TERMINAL_TEST_HOOKS
        runner.runSuite("Terminal capabilities and queries", testCapabilitiesAndQueries);
        runner.runSuite("Terminal text and style output", testTextAndStyleOutput);
        runner.runSuite("Terminal segmented and byte output", testSegmentedAndByteOutput);
        runner.runSuite("Terminal controls", testControls);
        runner.runSuite("Terminal input reads", testInputReads);
#if defined(_WIN32)
        runner.runSuite("Terminal stdin endpoint replacement", testInputEndpointReplacement);
#endif
        runner.runSuite("Terminal input modes", testInputModes);
#else
        runner.runSuite("Terminal hook-dependent suites", testHookDependentSuitesSkipped);
#endif

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Terminal library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
