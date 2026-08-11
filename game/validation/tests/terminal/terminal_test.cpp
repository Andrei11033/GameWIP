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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <latch>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
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

/// @brief Marker value whose formatter performs nested Session or global Terminal output.
struct TerminalSessionReentrantFormat
{
    GameWIP::Terminal::Session *session = nullptr;
    bool useGlobalOutput = false;
};

/// @brief Marker value whose formatter attempts to close its own active Session operation.
struct TerminalSessionCloseFormat
{
    GameWIP::Terminal::Session *session = nullptr;
    GameWIP::IO::Types::ErrorCode *closeCode = nullptr;
};

/// @brief Marker value whose formatter pauses so close-wait behavior can be observed deterministically.
struct TerminalSessionBlockingFormat
{
    std::latch *entered = nullptr;
    std::latch *release = nullptr;
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
        auto output = std::format_to(context.out(), "partial");
        throw std::format_error("terminal test formatter failure");
        return output;
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

template <> struct std::formatter<TerminalSessionReentrantFormat>
{
    constexpr auto parse(std::format_parse_context &context)
    {
        return context.begin();
    }

    template <typename FormatContext> auto format(const TerminalSessionReentrantFormat &value, FormatContext &context) const
    {
        const GameWIP::IO::Types::Status status = value.useGlobalOutput ? GameWIP::Terminal::writeText("global") : value.session->writeText("inner");
        if (!status.ok())
        {
            throw std::runtime_error("terminal Session reentrant formatter write failed");
        }
        return std::format_to(context.out(), "outer");
    }
};

template <> struct std::formatter<TerminalSessionCloseFormat>
{
    constexpr auto parse(std::format_parse_context &context)
    {
        return context.begin();
    }

    template <typename FormatContext> auto format(const TerminalSessionCloseFormat &value, FormatContext &context) const
    {
        *value.closeCode = value.session->close().code;
        return std::format_to(context.out(), "outer");
    }
};

template <> struct std::formatter<TerminalSessionBlockingFormat>
{
    constexpr auto parse(std::format_parse_context &context)
    {
        return context.begin();
    }

    template <typename FormatContext> auto format(const TerminalSessionBlockingFormat &value, FormatContext &context) const
    {
        value.entered->count_down();
        value.release->wait();
        return std::format_to(context.out(), "blocked-format");
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
    inline constexpr std::string_view kSessionReentrantFormatChildArgument = "--terminal-test-child=session-reentrant-format";

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

    /// @brief Exercises Session formatter reentry and same-operation close behavior in an isolated child.
    [[nodiscard]] int runSessionReentrantFormatChild()
    {
        Terminal::Types::SessionOptions sessionOptions;
        sessionOptions.deliveryMode = Terminal::Types::InputDeliveryMode::Stream;

        Terminal::Session session;
        if (!session.open(sessionOptions).ok())
        {
            return 1;
        }
        if (!session.print("{}", TerminalSessionReentrantFormat{.session = &session}).ok())
        {
            return 2;
        }

        Terminal::Types::LineWriteOptions lineOptions;
        lineOptions.lineEnding = Terminal::Types::LineEnding::Lf;
        if (!session.println(lineOptions, "{}", TerminalSessionReentrantFormat{.session = &session}).ok())
        {
            return 3;
        }
        if (!session.print("{}", TerminalSessionReentrantFormat{.session = &session, .useGlobalOutput = true}).ok())
        {
            return 4;
        }

        ErrorCode closeCode = ErrorCode::Unknown;
        if (!session.print("{}", TerminalSessionCloseFormat{.session = &session, .closeCode = &closeCode}).ok() ||
            closeCode != ErrorCode::ResourceBusy || !session.isOpen())
        {
            return 5;
        }
        return session.close().ok() ? 0 : 6;
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

        child.arguments = {std::string(kSessionReentrantFormatChildArgument)};
        const TestSupport::Types::ChildProcessResult sessionResult = TestSupport::runChildProcess(child);
        static_cast<void>(context.expectTrue("Session reentrant formatter child infrastructure succeeds", sessionResult.status.ok()));
        static_cast<void>(
            context.expectEq("Session reentrant formatter child exits", TestSupport::Types::ChildProcessOutcome::Exited, sessionResult.outcome));
        static_cast<void>(context.expectEq("Session reentrant formatter child returns zero", std::uint32_t{0}, sessionResult.exitCode));
        static_cast<void>(context.expectEq(
            "Session formatter supports nested Session/global output and checked close",
            std::string{"innerouterinnerouter\nglobalouterouter"},
            sessionResult.output));
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

    /// @brief Exercises wrapped managed-line editing, viewport scroll, and live resize on a real terminal.
    void testManualWrappedLineEditing(TestSupport::Context &context)
    {
        const Terminal::Types::TerminalSizeResult size = Terminal::getTerminalSize();
        const Terminal::Types::CursorPositionResult position = Terminal::getCursorPosition();
        if (!requireManualOperation(context, "manual wrapped line editing", "query terminal size", size.status) ||
            !requireManualOperation(context, "manual wrapped line editing", "query cursor position", position.status))
        {
            return;
        }

        // Place the prompt close enough to the viewport bottom that wrapping exercises visible scrolling.
        if (size.size.rows > 2 && position.position.row + 2 < size.size.rows)
        {
            const std::uint32_t blankLines = size.size.rows - position.position.row - 2;
            for (std::uint32_t line = 0; line < blankLines; ++line)
            {
                if (!requireManualOperation(context, "manual wrapped line editing", "position prompt near viewport bottom", Terminal::writeLine()))
                {
                    return;
                }
            }
        }

        const std::uint32_t minimumCharacters = size.size.columns + 10;
        context.manual(
            std::format(
                "Wrapped input check: type at least {} ASCII characters. Before Enter, use Left, Home, End, Backspace, and Delete to edit both "
                "rows; resize the terminal narrower and wider while the line is active. Watch for stale or misplaced text.",
                minimumCharacters));
        if (!requireManualOperation(context, "manual wrapped line editing", "wrapped-input prompt", Terminal::writeText("Wrapped/resize input: ")))
        {
            return;
        }

        const Terminal::Types::LineReadResult result = Terminal::readLine();
        if (!requireManualOperation(context, "manual wrapped line editing", "wrapped readLine", result.status))
        {
            return;
        }
        if (result.outcome != Terminal::Types::ReadOutcome::Completed || result.line.size() < minimumCharacters)
        {
            context.fail("manual wrapped line editing", "the completed line was not long enough to prove wrapping");
            return;
        }

        recordManualCheck(
            context,
            "manual wrapped line editing",
            "Did wrapping, scrolling, navigation, editing, and redraw remain coherent before and after resize, with no stale text?");
    }

    /// @brief Verifies native structured key delivery through a real interactive terminal session.
    void testManualEventInput(TestSupport::Context &context, const Terminal::Types::InputCapabilities &inputCapabilities)
    {
        if (!inputCapabilities.supportsEventInput)
        {
            context.skip("manual structured input", "the terminal does not report structured event support");
            return;
        }

        if (!requireManualOperation(
                context,
                "manual structured input",
                "event prompt writeText",
                Terminal::writeText("Event input: press the Left Arrow key: ")))
        {
            return;
        }

        Terminal::Session session;
        const IO::Types::Status openStatus = session.open();
        if (!requireManualOperation(context, "manual structured input", "open event session", openStatus))
        {
            return;
        }

        Terminal::Types::EventReadOptions options;
        options.timeout = std::chrono::seconds{10};

        bool matched = false;
        for (int attempt = 0; attempt < 8 && !matched; ++attempt)
        {
            const Terminal::Types::EventReadResult event = session.readEvent(options);
            if (!event.status.ok())
            {
                static_cast<void>(requireManualOperation(context, "manual structured input", "readEvent", event.status));
                break;
            }
            if (event.outcome != Terminal::Types::ReadOutcome::Completed || !event.event.has_value())
            {
                context.fail("manual structured input", "readEvent did not produce a key event before the deadline");
                break;
            }

            const Terminal::Types::KeyEvent *key = event.event->getIf<Terminal::Types::KeyEvent>();
            if (key == nullptr || key->action == Terminal::Types::KeyAction::Release)
            {
                continue;
            }

            const auto *named = std::get_if<Terminal::Types::NamedKey>(&key->key);
            matched = named != nullptr && *named == Terminal::Types::NamedKey::ArrowLeft;
        }

        const bool closed = requireManualOperation(context, "manual structured input", "close event session", session.close());
        if (closed)
        {
            static_cast<void>(Terminal::writeLine());
        }

        if (matched && closed)
        {
            context.pass("manual structured input");
        }
        else if (closed)
        {
            context.fail("manual structured input", "the observed key was not the portable Left Arrow event");
        }
    }

    /// @brief Verifies managed Session input restoration and cursor visibility restoration.
    void testManualStateRestoration(
        TestSupport::Context &context,
        const Terminal::Types::InputCapabilities &inputCapabilities,
        const Terminal::Types::OutputCapabilities &outputCapabilities)
    {
        if (!inputCapabilities.supportsLineInput)
        {
            context.skip("manual session restoration", "the terminal does not report managed line input support");
        }
        else
        {
            Terminal::Session session;
            Terminal::Types::SessionOptions options;
            options.deliveryMode = Terminal::Types::InputDeliveryMode::Stream;

            const IO::Types::Status openStatus = session.open(options);
            if (requireManualOperation(context, "manual session restoration", "open managed input session", openStatus))
            {
                if (!requireManualOperation(
                        context,
                        "manual session restoration",
                        "session-input prompt writeText",
                        Terminal::writeText("State restoration: type hidden and press Enter: ")))
                {
                    static_cast<void>(session.close());
                    return;
                }

                const Terminal::Types::LineReadResult hiddenInput = session.readLine();
                bool readSucceeded = requireManualOperation(context, "manual session restoration", "read managed session input", hiddenInput.status);
                if (readSucceeded && (hiddenInput.outcome != Terminal::Types::ReadOutcome::Completed || hiddenInput.line != "hidden"))
                {
                    context.fail("manual session restoration", "managed hidden input did not produce the requested line");
                    readSucceeded = false;
                }

                const bool closeSucceeded =
                    requireManualOperation(context, "manual session restoration", "close and restore managed session", session.close());
                if (readSucceeded && closeSucceeded)
                {
                    recordManualCheck(
                        context,
                        "manual session restoration",
                        "Did input behave normally during the Stream session, and is normal terminal input behavior still intact after close?");
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
        testManualWrappedLineEditing(context);
        testManualEventInput(context, input.capabilities);
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

        const Terminal::Types::SessionOptions sessionDefaults;
        static_cast<void>(context.expectEq("session defaults to stdin", Terminal::Types::InputStream::Stdin, sessionDefaults.input));
        static_cast<void>(context.expectEq("session defaults to stdout", Terminal::Types::OutputStream::Stdout, sessionDefaults.output));
        static_cast<void>(
            context.expectEq("session defaults to event delivery", Terminal::Types::InputDeliveryMode::Events, sessionDefaults.deliveryMode));
        static_cast<void>(context.expectEq(
            "session defaults to native control processing",
            Terminal::Types::ControlKeyMode::NativeProcessing,
            sessionDefaults.controlKeyMode));

        constexpr Terminal::Types::KeyModifier modifiers = Terminal::Types::KeyModifier::Shift | Terminal::Types::KeyModifier::Control;
        static_cast<void>(context.expectTrue(
            "key modifier bitmask reports present shift",
            Terminal::Types::hasModifier(modifiers, Terminal::Types::KeyModifier::Shift)));
        static_cast<void>(context.expectTrue(
            "key modifier bitmask reports present control",
            Terminal::Types::hasModifier(modifiers, Terminal::Types::KeyModifier::Control)));
        static_cast<void>(context.expectFalse(
            "key modifier bitmask reports absent alt",
            Terminal::Types::hasModifier(modifiers, Terminal::Types::KeyModifier::Alt)));

        Terminal::Types::KeyEvent keyEvent;
        keyEvent.key = Terminal::Types::CharacterKey{U'\u03BB'};
        keyEvent.modifiers = modifiers;
        keyEvent.action = Terminal::Types::KeyAction::Press;
        keyEvent.location = Terminal::Types::KeyLocation::Standard;
        Terminal::Types::Event event{.data = keyEvent};

        const Terminal::Types::KeyEvent *storedKeyEvent = event.getIf<Terminal::Types::KeyEvent>();
        static_cast<void>(context.expectTrue("event getIf returns matching key event", storedKeyEvent != nullptr));
        static_cast<void>(context.expectTrue("event getIf rejects paste alternative", event.getIf<Terminal::Types::PasteEvent>() == nullptr));
        if (storedKeyEvent != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::CharacterKey>(&storedKeyEvent->key);
            static_cast<void>(context.expectTrue("key variant stores character alternative", character != nullptr));
            if (character != nullptr)
            {
                static_cast<void>(context.expectTrue("character key preserves Unicode scalar", character->value == U'\u03BB'));
            }
            static_cast<void>(context.expectEq("key event preserves modifiers", modifiers, storedKeyEvent->modifiers));
            static_cast<void>(context.expectEq("key event preserves action", Terminal::Types::KeyAction::Press, storedKeyEvent->action));
            static_cast<void>(context.expectEq("key event preserves location", Terminal::Types::KeyLocation::Standard, storedKeyEvent->location));
            static_cast<void>(context.expectEq("key event defaults to one occurrence", std::uint32_t{1}, storedKeyEvent->repeatCount));
        }

        keyEvent.key = Terminal::Types::FunctionKey{24};
        event.data = keyEvent;
        storedKeyEvent = event.getIf<Terminal::Types::KeyEvent>();
        static_cast<void>(context.expectTrue("event getIf preserves function-key event", storedKeyEvent != nullptr));
        if (storedKeyEvent != nullptr)
        {
            const auto *functionKey = std::get_if<Terminal::Types::FunctionKey>(&storedKeyEvent->key);
            static_cast<void>(context.expectTrue("key variant stores function-key alternative", functionKey != nullptr));
            if (functionKey != nullptr)
            {
                static_cast<void>(context.expectEq("function key preserves numeric value", std::uint16_t{24}, functionKey->number));
            }
        }

        event.data = Terminal::Types::PasteEvent{.text = "paste"};
        const Terminal::Types::PasteEvent *paste = event.getIf<Terminal::Types::PasteEvent>();
        static_cast<void>(context.expectTrue("event getIf returns paste event", paste != nullptr));
        if (paste != nullptr)
        {
            static_cast<void>(context.expectEq("paste event preserves UTF-8 text", std::string{"paste"}, paste->text));
        }

        event.data = Terminal::Types::ResizeEvent{.size = {.columns = 120, .rows = 40}};
        const Terminal::Types::ResizeEvent *resize = event.getIf<Terminal::Types::ResizeEvent>();
        static_cast<void>(context.expectTrue("event getIf returns resize event", resize != nullptr));
        if (resize != nullptr)
        {
            static_cast<void>(context.expectEq("resize event preserves columns", std::uint32_t{120}, resize->size.columns));
            static_cast<void>(context.expectEq("resize event preserves rows", std::uint32_t{40}, resize->size.rows));
        }

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

    /// @brief Returns fully managed interactive-input capabilities for hook-backed tests.
    [[nodiscard]] Terminal::Types::InputCapabilities terminalInputCapabilities() noexcept
    {
        return {
            .kind = Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteInput = true,
            .supportsLineInput = true,
            .supportsEventInput = true,
            .supportsNonBlockingReads = true,
            .supportsFiniteTimeouts = true,
            .supportsCancellation = true,
            .supportsResizeEvents = true,
            .supportsPasteEvents = false,
            .supportsKeyRepeatEvents = true,
            .supportsKeyReleaseEvents = true,
            .supportsStandaloneModifierEvents = true,
            .supportsMediaKeyEvents = false,
            .supportsKeyLocation = true,
            .supportsModifierState = true};
    }

    /// @brief Returns redirected stream capabilities for byte/text/line hook fixtures.
    [[nodiscard]] Terminal::Types::InputCapabilities redirectedInputCapabilities() noexcept
    {
        return {
            .kind = Terminal::Types::StreamKind::Redirected,
            .supportsUtf8Text = true,
            .supportsByteInput = true,
            .supportsLineInput = true,
            .supportsEventInput = false,
            .supportsNonBlockingReads = true,
            .supportsFiniteTimeouts = true,
            .supportsCancellation = true};
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
        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, redirectedInputCapabilities());
        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, bytes, endOfStreamWhenEmpty);
    }

    /// @brief Verifies capability observation plus output preparation, size, and position queries.
    void testCapabilitiesAndQueries(TestSupport::Context &context)
    {
        Hooks::reset();

        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
        const Terminal::Types::InputCapabilitiesResult inputCapabilities = Terminal::getInputCapabilities();
        static_cast<void>(context.expectTrue("input capabilities status", inputCapabilities.status.ok()));
        static_cast<void>(context.expectEq("input capability kind", Terminal::Types::StreamKind::Terminal, inputCapabilities.capabilities.kind));
        static_cast<void>(context.expectTrue("input capability event support", inputCapabilities.capabilities.supportsEventInput));
        static_cast<void>(context.expectTrue("input capability cancellation support", inputCapabilities.capabilities.supportsCancellation));

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

        Terminal::OutputBuffer lineEndingBuffer;
        static_cast<void>(
            context.expectEq("output buffer defaults to native line ending", Terminal::Types::LineEnding::Native, lineEndingBuffer.lineEnding()));
        static_cast<void>(context.expectEq(
            "output buffer rejects invalid line ending without throwing",
            ErrorCode::InvalidArgument,
            lineEndingBuffer.setLineEnding(static_cast<Terminal::Types::LineEnding>(-1)).code));
        static_cast<void>(
            context.expectEq("invalid line ending preserves previous setting", Terminal::Types::LineEnding::Native, lineEndingBuffer.lineEnding()));

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
        Terminal::OutputBuffer outputBuffer;
        static_cast<void>(context.expectTrue("output buffer sets LF line ending", outputBuffer.setLineEnding(Terminal::Types::LineEnding::Lf).ok()));
        static_cast<void>(context.expectTrue("output buffer reserve succeeds", outputBuffer.reserve(64).ok()));
        static_cast<void>(context.expectTrue("output buffer appendText succeeds", outputBuffer.appendText("alpha").ok()));
        static_cast<void>(context.expectTrue("output buffer appendLine succeeds", outputBuffer.appendLine(" beta").ok()));
        static_cast<void>(context.expectTrue("output buffer print succeeds", outputBuffer.print("{}", 3).ok()));
        static_cast<void>(context.expectTrue("output buffer println succeeds", outputBuffer.println(" {}", 4).ok()));
        static_cast<void>(context.expectEq("output buffer text", std::string_view{"alpha beta\n3 4\n"}, outputBuffer.text()));

        const std::string beforeFormattingFailure(outputBuffer.text());
        static_cast<void>(context.expectEq(
            "output buffer formatting failure returns status",
            ErrorCode::InvalidArgument,
            outputBuffer.print("{}", TerminalThrowingFormat{}).code));
        static_cast<void>(
            context.expectEq("output buffer formatting failure rolls back partial record", beforeFormattingFailure, outputBuffer.text()));
        static_cast<void>(context.expectEq(
            "output buffer println failure returns status",
            ErrorCode::InvalidArgument,
            outputBuffer.println("{}", TerminalThrowingFormat{}).code));
        static_cast<void>(context.expectEq(
            "output buffer println failure rolls back formatted text and line ending",
            beforeFormattingFailure,
            outputBuffer.text()));
        static_cast<void>(context.expectEq(
            "output buffer oversized reserve is checked",
            ErrorCode::SizeLimitExceeded,
            outputBuffer.reserve(std::numeric_limits<std::size_t>::max()).code));
        static_cast<void>(context.expectEq("output buffer reserve failure preserves contents", beforeFormattingFailure, outputBuffer.text()));

        static_cast<void>(context.expectTrue("output buffer flush succeeds", outputBuffer.flushTo().ok()));
        static_cast<void>(context.expectTrue("output buffer clears after flush", outputBuffer.empty()));
        static_cast<void>(context.expectEq(
            "output buffer flush capture",
            std::string{"alpha beta\n3 4\n"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        static_cast<void>(context.expectTrue("output buffer retry append succeeds", outputBuffer.appendText("retry").ok()));
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
            Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
            Terminal::CursorHiddenScope scope = Terminal::scopedCursorHidden({.flushMode = IO::Types::FlushMode::Data});
            static_cast<void>(context.expectEq("cursor setup flush failure propagates", ErrorCode::FlushFailed, scope.status().code));
            static_cast<void>(context.expectTrue("cursor setup flush failure retains restoration ownership", scope.active()));
            static_cast<void>(context.expectTrue("cursor setup flush failure can still restore", scope.restore().ok()));
        }
        static_cast<void>(context.expectEq(
            "cursor setup flush failure does not leak hidden state",
            std::string{"\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::CursorHiddenScope scope = Terminal::scopedCursorHidden({.flushMode = IO::Types::FlushMode::Data});
            Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
            static_cast<void>(context.expectEq("cursor restore flush failure propagates", ErrorCode::FlushFailed, scope.restore().code));
            static_cast<void>(context.expectTrue("cursor restore flush failure remains retryable", scope.active()));
            static_cast<void>(context.expectTrue("cursor restore flush retry succeeds", scope.restore().ok()));
            static_cast<void>(context.expectFalse("cursor restore flush retry releases ownership", scope.active()));
        }
        static_cast<void>(context.expectEq(
            "cursor restore flush retry does not repeat the show transition",
            std::string{"\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::CursorHiddenScope failedDestructorScope = Terminal::scopedCursorHidden();
            Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
        }
        {
            Terminal::CursorHiddenScope laterScope = Terminal::scopedCursorHidden();
            static_cast<void>(context.expectTrue("scope nesting remains usable after destructor restoration failure", laterScope.restore().ok()));
        }
        static_cast<void>(context.expectEq(
            "failed scope destructor releases stale nesting ownership",
            std::string{"\x1b[?25l\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

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
    /// @brief Builds one deterministic Win32 key record for native event-decoder validation.
    [[nodiscard]] KEY_EVENT_RECORD makeWin32KeyRecord(
        bool keyDown,
        WORD virtualKey,
        wchar_t unicodeCharacter = L'\0',
        DWORD controlState = 0,
        WORD repeatCount = 1,
        WORD scanCode = 0) noexcept
    {
        KEY_EVENT_RECORD record{};
        record.bKeyDown = keyDown ? TRUE : FALSE;
        record.wRepeatCount = repeatCount;
        record.wVirtualKeyCode = virtualKey;
        record.wVirtualScanCode = scanCode;
        record.uChar.UnicodeChar = unicodeCharacter;
        record.dwControlKeyState = controlState;
        return record;
    }

    /// @brief Verifies Win32 native key records normalize into the portable Terminal key contract.
    void testWin32EventDecoder(TestSupport::Context &context)
    {
        const auto expectKeyEvent =
            [&context](std::string_view label, const Hooks::Win32KeyDecodeResult &decoded) -> const Terminal::Types::KeyEvent *
        {
            static_cast<void>(context.expectTrue(std::format("{} status", label), decoded.status.ok()));
            static_cast<void>(
                context.expectEq(std::format("{} disposition", label), Hooks::Win32KeyDecodeDisposition::Produced, decoded.disposition));

            if (!decoded.event.has_value())
            {
                context.fail(std::string(label), "decoder produced no portable event");
                return nullptr;
            }

            const Terminal::Types::KeyEvent *key = decoded.event->getIf<Terminal::Types::KeyEvent>();
            static_cast<void>(context.expectTrue(std::format("{} payload", label), key != nullptr));
            return key;
        };

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'A', u'a');
            const Terminal::Types::KeyEvent *key = expectKeyEvent("character A", decoded);
            if (key != nullptr)
            {
                const auto *character = std::get_if<Terminal::Types::CharacterKey>(&key->key);
                static_cast<void>(context.expectTrue("character A alternative", character != nullptr));
                if (character != nullptr)
                {
                    static_cast<void>(context.expectEq("character A scalar", U'a', character->value));
                }
                static_cast<void>(context.expectEq("character A press", Terminal::Types::KeyAction::Press, key->action));
                static_cast<void>(context.expectEq("character A standard location", Terminal::Types::KeyLocation::Standard, key->location));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'A', static_cast<char16_t>(0x0001), LEFT_CTRL_PRESSED);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("Ctrl+A", decoded);
            if (key != nullptr)
            {
                const auto *character = std::get_if<Terminal::Types::CharacterKey>(&key->key);
                static_cast<void>(context.expectTrue("Ctrl+A character alternative", character != nullptr));
                if (character != nullptr)
                {
                    static_cast<void>(context.expectEq("Ctrl+A normalized scalar", U'a', character->value));
                }
                static_cast<void>(context.expectTrue(
                    "Ctrl+A control modifier",
                    Terminal::Types::hasModifier(key->modifiers, Terminal::Types::KeyModifier::Control)));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'Q', u'@', RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("AltGr character", decoded);
            if (key != nullptr)
            {
                const auto *character = std::get_if<Terminal::Types::CharacterKey>(&key->key);
                static_cast<void>(context.expectTrue("AltGr character alternative", character != nullptr));
                if (character != nullptr)
                {
                    static_cast<void>(context.expectEq("AltGr translated scalar", U'@', character->value));
                }
                static_cast<void>(context.expectFalse(
                    "AltGr removes synthetic control",
                    Terminal::Types::hasModifier(key->modifiers, Terminal::Types::KeyModifier::Control)));
                static_cast<void>(context.expectFalse(
                    "AltGr removes synthetic alt",
                    Terminal::Types::hasModifier(key->modifiers, Terminal::Types::KeyModifier::Alt)));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, VK_LCONTROL, u'\0', LEFT_CTRL_PRESSED);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("left control", decoded);
            if (key != nullptr)
            {
                const auto *modifier = std::get_if<Terminal::Types::ModifierKey>(&key->key);
                static_cast<void>(context.expectTrue("left control modifier alternative", modifier != nullptr));
                if (modifier != nullptr)
                {
                    static_cast<void>(context.expectEq("left control logical key", Terminal::Types::ModifierKey::Control, *modifier));
                }
                static_cast<void>(context.expectEq("left control location", Terminal::Types::KeyLocation::Left, key->location));
                static_cast<void>(context.expectFalse(
                    "standalone modifier excludes itself",
                    Terminal::Types::hasModifier(key->modifiers, Terminal::Types::KeyModifier::Control)));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult left = Hooks::decodeWin32KeyRecord(true, VK_CONTROL, u'\0', LEFT_CTRL_PRESSED);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("generic left control", left);
            if (key != nullptr)
            {
                static_cast<void>(context.expectEq("generic left control press", Terminal::Types::KeyAction::Press, key->action));
                static_cast<void>(context.expectEq("generic left control location", Terminal::Types::KeyLocation::Left, key->location));
            }

            const Hooks::Win32KeyDecodeResult right =
                Hooks::decodeWin32KeyRecord(true, VK_CONTROL, u'\0', LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED | ENHANCED_KEY);
            key = expectKeyEvent("generic right control", right);
            if (key != nullptr)
            {
                static_cast<void>(context.expectEq("other-side control is a press", Terminal::Types::KeyAction::Press, key->action));
                static_cast<void>(context.expectEq("generic right control location", Terminal::Types::KeyLocation::Right, key->location));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult super = Hooks::decodeWin32KeyRecord(true, VK_LWIN);
            static_cast<void>(expectKeyEvent("left super", super));

            const Hooks::Win32KeyDecodeResult character = Hooks::decodeWin32KeyRecord(true, 'A', u'a');
            const Terminal::Types::KeyEvent *key = expectKeyEvent("Super+A", character);
            if (key != nullptr)
            {
                static_cast<void>(context.expectTrue(
                    "tracked super accompanies following key",
                    Terminal::Types::hasModifier(key->modifiers, Terminal::Types::KeyModifier::Super)));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult dedicated = Hooks::decodeWin32KeyRecord(true, VK_LEFT, u'\0', ENHANCED_KEY);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("dedicated left arrow", dedicated);
            if (key != nullptr)
            {
                const auto *named = std::get_if<Terminal::Types::NamedKey>(&key->key);
                static_cast<void>(context.expectTrue("left arrow named alternative", named != nullptr));
                if (named != nullptr)
                {
                    static_cast<void>(context.expectEq("left arrow logical key", Terminal::Types::NamedKey::ArrowLeft, *named));
                }
                static_cast<void>(context.expectEq("dedicated arrow location", Terminal::Types::KeyLocation::Standard, key->location));
            }

            Hooks::resetWin32KeyDecoder();
            const Hooks::Win32KeyDecodeResult numpad = Hooks::decodeWin32KeyRecord(true, VK_LEFT);
            key = expectKeyEvent("numpad left arrow", numpad);
            if (key != nullptr)
            {
                static_cast<void>(context.expectEq("numpad arrow location", Terminal::Types::KeyLocation::Numpad, key->location));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, VK_F24);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("F24", decoded);
            if (key != nullptr)
            {
                const auto *function = std::get_if<Terminal::Types::FunctionKey>(&key->key);
                static_cast<void>(context.expectTrue("F24 function alternative", function != nullptr));
                if (function != nullptr)
                {
                    static_cast<void>(context.expectEq("F24 number", std::uint16_t{24}, function->number));
                }
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult first = Hooks::decodeWin32KeyRecord(true, 'A', u'a', 0, 3);
            const Terminal::Types::KeyEvent *key = expectKeyEvent("combined repeat press", first);
            if (key != nullptr)
            {
                static_cast<void>(context.expectEq("combined repeat first action", Terminal::Types::KeyAction::Press, key->action));
                static_cast<void>(context.expectEq("combined repeat press count", std::uint32_t{1}, key->repeatCount));
            }

            const std::optional<Terminal::Types::Event> pending = Hooks::takePendingWin32KeyEvent();
            static_cast<void>(context.expectTrue("combined repeat retains follow-up event", pending.has_value()));
            if (pending.has_value())
            {
                const Terminal::Types::KeyEvent *repeat = pending->getIf<Terminal::Types::KeyEvent>();
                static_cast<void>(context.expectTrue("combined repeat follow-up payload", repeat != nullptr));
                if (repeat != nullptr)
                {
                    static_cast<void>(context.expectEq("combined repeat action", Terminal::Types::KeyAction::Repeat, repeat->action));
                    static_cast<void>(context.expectEq("combined repeat count", std::uint32_t{2}, repeat->repeatCount));
                }
            }

            const Hooks::Win32KeyDecodeResult released = Hooks::decodeWin32KeyRecord(false, 'A', u'a');
            key = expectKeyEvent("character release", released);
            if (key != nullptr)
            {
                static_cast<void>(context.expectEq("character release action", Terminal::Types::KeyAction::Release, key->action));
            }
        }

        Hooks::resetWin32KeyDecoder();
        {
            const Hooks::Win32KeyDecodeResult high = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xd83d));
            static_cast<void>(context.expectEq("surrogate high waits", Hooks::Win32KeyDecodeDisposition::Pending, high.disposition));

            const Hooks::Win32KeyDecodeResult low = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xde00));
            const Terminal::Types::KeyEvent *key = expectKeyEvent("surrogate pair", low);
            if (key != nullptr)
            {
                const auto *character = std::get_if<Terminal::Types::CharacterKey>(&key->key);
                static_cast<void>(context.expectTrue("surrogate pair character alternative", character != nullptr));
                if (character != nullptr)
                {
                    static_cast<void>(context.expectEq("surrogate pair scalar", static_cast<char32_t>(0x1f600), character->value));
                }
            }

            Hooks::resetWin32KeyDecoder();
            const Hooks::Win32KeyDecodeResult malformed = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xde00));
            static_cast<void>(context.expectEq("lone low surrogate fails", ErrorCode::EncodingFailed, malformed.status.code));
            static_cast<void>(context.expectEq("lone low surrogate disposition", Hooks::Win32KeyDecodeDisposition::Failed, malformed.disposition));
        }
    }

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
        static_cast<void>(context.expectEq("detached stdin reports NotOpen after state reset", ErrorCode::NotOpen, Terminal::readText().status.code));

        static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
        CloseHandle(replacementRead);
        Hooks::reset();
    }
#endif

    /// @brief Verifies the managed Unicode line editor over deterministic structured events.
    void testManagedLineEditing(TestSupport::Context &context)
    {
        const auto characterEvent =
            [](char32_t scalar, Terminal::Types::KeyAction action = Terminal::Types::KeyAction::Press, std::uint32_t repeat = 1)
        {
            Terminal::Types::KeyEvent key;
            key.key = Terminal::Types::CharacterKey{.value = scalar};
            key.action = action;
            key.location = Terminal::Types::KeyLocation::Standard;
            key.repeatCount = repeat;
            return Terminal::Types::Event{.data = key};
        };

        const auto namedEvent =
            [](Terminal::Types::NamedKey named, Terminal::Types::KeyAction action = Terminal::Types::KeyAction::Press, std::uint32_t repeat = 1)
        {
            Terminal::Types::KeyEvent key;
            key.key = named;
            key.action = action;
            key.location = Terminal::Types::KeyLocation::Standard;
            key.repeatCount = repeat;
            return Terminal::Types::Event{.data = key};
        };

        const auto runLine = [&context](std::span<const Terminal::Types::Event> events, const Terminal::Types::LineReadOptions &options)
        {
            Hooks::reset();
            Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
            Hooks::setInputModeOverride(Terminal::Types::InputStream::Stdin, true, true, true);
            Hooks::setInputEvents(Terminal::Types::InputStream::Stdin, events);

            Terminal::Types::SessionOptions sessionOptions;
            sessionOptions.deliveryMode = Terminal::Types::InputDeliveryMode::Stream;

            Terminal::Session session;
            const IO::Types::Status openStatus = session.open(sessionOptions);
            static_cast<void>(context.expectTrue("managed line session opens", openStatus.ok()));
            if (!openStatus.ok())
            {
                return Terminal::Types::LineReadResult{.status = openStatus};
            }

            Terminal::Types::LineReadResult result = session.readLine(options);
            const IO::Types::Status closeStatus = session.close();
            static_cast<void>(context.expectTrue("managed line session closes", closeStatus.ok()));
            return result;
        };

        struct EchoRunResult
        {
            Terminal::Types::LineReadResult line;
            std::vector<Terminal::Types::CursorPosition> cursorSets;
            Terminal::Types::CursorPosition viewportOrigin{};
        };

        const auto runEchoLine =
            [&context](std::span<const Terminal::Types::Event> events, Terminal::Types::TerminalSize size, Terminal::Types::CursorPosition position)
        {
            Hooks::reset();
            Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
            Hooks::setInputModeOverride(Terminal::Types::InputStream::Stdin, true, true, true);
            Hooks::setInputEvents(Terminal::Types::InputStream::Stdin, events);
            setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
            Hooks::enableCursorRenderingSimulation(Terminal::Types::OutputStream::Stdout, size, position);

            Terminal::Types::SessionOptions sessionOptions;
            sessionOptions.deliveryMode = Terminal::Types::InputDeliveryMode::Stream;

            Terminal::Session session;
            const IO::Types::Status openStatus = session.open(sessionOptions);
            static_cast<void>(context.expectTrue("managed echo session opens", openStatus.ok()));
            if (!openStatus.ok())
            {
                return EchoRunResult{.line = {.status = openStatus}};
            }

            Terminal::Types::LineReadOptions echoOptions;
            echoOptions.echo = true;
            EchoRunResult result;
            result.line = session.readLine(echoOptions);
            result.cursorSets = Hooks::cursorRenderingSetHistory(Terminal::Types::OutputStream::Stdout);
            result.viewportOrigin = Hooks::cursorRenderingViewportOrigin(Terminal::Types::OutputStream::Stdout);
            static_cast<void>(context.expectTrue("managed echo session closes", session.close().ok()));
            return result;
        };

        Terminal::Types::LineReadOptions options;
        options.echo = false;

        {
            const std::array<Terminal::Types::Event, 9> events{
                characterEvent(U'a'),
                characterEvent(U'b'),
                characterEvent(U'c'),
                namedEvent(Terminal::Types::NamedKey::ArrowLeft),
                characterEvent(U'X'),
                namedEvent(Terminal::Types::NamedKey::Home),
                namedEvent(Terminal::Types::NamedKey::Delete),
                namedEvent(Terminal::Types::NamedKey::End),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const Terminal::Types::LineReadResult result = runLine(events, options);
            static_cast<void>(context.expectTrue("managed edit status", result.status.ok()));
            static_cast<void>(context.expectEq("managed edit outcome", Terminal::Types::ReadOutcome::Completed, result.outcome));
            static_cast<void>(context.expectEq("managed edit text", std::string{"bXc"}, result.line));
        }

        {
            const std::array<Terminal::Types::Event, 5> events{
                characterEvent(U'a'),
                characterEvent(static_cast<char32_t>(0x0308)),
                namedEvent(Terminal::Types::NamedKey::Backspace),
                characterEvent(U'\u03bb'),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const Terminal::Types::LineReadResult result = runLine(events, options);
            static_cast<void>(context.expectTrue("grapheme backspace status", result.status.ok()));
            static_cast<void>(context.expectEq("grapheme backspace removes whole cluster", std::string{"\xce\xbb"}, result.line));
        }

        {
            const std::array<Terminal::Types::Event, 6> events{
                characterEvent(static_cast<char32_t>(0x1f469)),
                characterEvent(static_cast<char32_t>(0x1f4bb)),
                namedEvent(Terminal::Types::NamedKey::ArrowLeft),
                characterEvent(static_cast<char32_t>(0x200d)),
                namedEvent(Terminal::Types::NamedKey::Backspace),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const Terminal::Types::LineReadResult result = runLine(events, options);
            static_cast<void>(context.expectTrue("middle grapheme merge status", result.status.ok()));
            static_cast<void>(context.expectEq("middle grapheme merge keeps caret on a cluster boundary", std::string{}, result.line));
        }

        {
            const std::array<Terminal::Types::Event, 3> events{
                characterEvent(U'z', Terminal::Types::KeyAction::Repeat, 3),
                namedEvent(Terminal::Types::NamedKey::Backspace, Terminal::Types::KeyAction::Repeat, 2),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const Terminal::Types::LineReadResult result = runLine(events, options);
            static_cast<void>(context.expectTrue("repeat edit status", result.status.ok()));
            static_cast<void>(context.expectEq("repeat edit count", std::string{"z"}, result.line));
        }

        {
            const std::array<Terminal::Types::Event, 2> events{
                Terminal::Types::Event{.data = Terminal::Types::PasteEvent{.text = std::string{"\xc3\xa9\xf0\x9f\x99\x82"}}},
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            Terminal::Types::LineReadOptions bounded = options;
            bounded.maxReturnedBytes = 3;
            const Terminal::Types::LineReadResult result = runLine(events, bounded);
            static_cast<void>(context.expectTrue("bounded paste status", result.status.ok()));
            static_cast<void>(context.expectEq("bounded paste preserves scalar boundary", std::string{"\xc3\xa9"}, result.line));
            static_cast<void>(context.expectTrue("bounded paste reports truncation", result.wasTruncated));
        }

        {
            const std::array<Terminal::Types::Event, 11> events{
                characterEvent(U'a'),
                characterEvent(U'b'),
                characterEvent(U'c'),
                characterEvent(U'd'),
                namedEvent(Terminal::Types::NamedKey::ArrowLeft),
                characterEvent(U'X'),
                namedEvent(Terminal::Types::NamedKey::Home),
                namedEvent(Terminal::Types::NamedKey::Delete),
                namedEvent(Terminal::Types::NamedKey::End),
                characterEvent(U'Y'),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const EchoRunResult result = runEchoLine(events, {.columns = 5, .rows = 3}, {.column = 3, .row = 2});
            static_cast<void>(context.expectTrue("wrapped edit status", result.line.status.ok()));
            static_cast<void>(context.expectEq("wrapped left/home edit text", std::string{"bcXdY"}, result.line.line));
            static_cast<void>(context.expectTrue("wrapped edit performs cursor redraw", !result.cursorSets.empty()));
            if (!result.cursorSets.empty())
            {
                static_cast<void>(
                    context.expectEq("wrapped redraw rebuilds stable origin column", std::uint32_t{3}, result.cursorSets.front().column));
                static_cast<void>(context.expectEq("wrapped redraw rebuilds stable origin row", std::uint32_t{2}, result.cursorSets.front().row));
            }
            static_cast<void>(context.expectTrue("wrapped typing simulates viewport scroll", result.viewportOrigin.row > 0));
        }

        {
            const std::array<Terminal::Types::Event, 8> events{
                characterEvent(U'a'),
                characterEvent(U'b'),
                characterEvent(U'c'),
                characterEvent(U'd'),
                namedEvent(Terminal::Types::NamedKey::Backspace),
                namedEvent(Terminal::Types::NamedKey::ArrowLeft),
                namedEvent(Terminal::Types::NamedKey::Delete),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const EchoRunResult result = runEchoLine(events, {.columns = 4, .rows = 3}, {.column = 3, .row = 1});
            static_cast<void>(context.expectTrue("wrapped backspace/delete status", result.line.status.ok()));
            static_cast<void>(context.expectEq("wrapped backspace/delete text", std::string{"ab"}, result.line.line));
            static_cast<void>(context.expectTrue("wrapped backspace/delete redraws from tracked anchor", !result.cursorSets.empty()));
        }

        {
            const std::array<Terminal::Types::Event, 9> events{
                characterEvent(U'a'),
                characterEvent(U'b'),
                characterEvent(U'c'),
                characterEvent(U'd'),
                Terminal::Types::Event{.data = Terminal::Types::ResizeEvent{.size = {.columns = 4, .rows = 3}}},
                namedEvent(Terminal::Types::NamedKey::Home),
                characterEvent(U'Z'),
                namedEvent(Terminal::Types::NamedKey::End),
                namedEvent(Terminal::Types::NamedKey::Enter),
            };

            const EchoRunResult result = runEchoLine(events, {.columns = 5, .rows = 3}, {.column = 3, .row = 1});
            static_cast<void>(context.expectTrue("resize redraw status", result.line.status.ok()));
            static_cast<void>(context.expectEq("resize redraw preserves editable text", std::string{"Zabcd"}, result.line.line));
            static_cast<void>(context.expectTrue("resize redraw performs cursor positioning", !result.cursorSets.empty()));
            if (!result.cursorSets.empty())
            {
                static_cast<void>(
                    context.expectEq("resize redraw rebuilds reflowed origin column", std::uint32_t{0}, result.cursorSets.front().column));
                static_cast<void>(context.expectEq("resize redraw rebuilds reflowed origin row", std::uint32_t{2}, result.cursorSets.front().row));
            }
        }

        {
            Hooks::reset();
            Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
            Hooks::setInputModeOverride(Terminal::Types::InputStream::Stdin, true, true, true);
            const std::array<Terminal::Types::Event, 1> events{characterEvent(U'p')};
            Hooks::setInputEvents(Terminal::Types::InputStream::Stdin, events, false);

            Terminal::Types::SessionOptions sessionOptions;
            sessionOptions.deliveryMode = Terminal::Types::InputDeliveryMode::Stream;
            Terminal::Session session;
            static_cast<void>(context.expectTrue("partial line session opens", session.open(sessionOptions).ok()));

            Terminal::Types::LineReadOptions partialOptions;
            partialOptions.echo = false;
            partialOptions.timeout = Terminal::kNoWait;
            const Terminal::Types::LineReadResult partial = session.readLine(partialOptions);
            static_cast<void>(context.expectTrue("partial line status", partial.status.ok()));
            static_cast<void>(context.expectEq("partial line would-block", Terminal::Types::ReadOutcome::WouldBlock, partial.outcome));
            static_cast<void>(context.expectEq("partial line preserves text", std::string{"p"}, partial.line));
            static_cast<void>(context.expectTrue("partial line session closes", session.close().ok()));
        }

        Hooks::reset();
    }

    /// @brief Verifies persistent Session lifecycle, ownership, cancellation, and exact restoration.
    void testSessions(TestSupport::Context &context)
    {
        Hooks::reset();
        Hooks::setInputCapabilitiesOverride(Terminal::Types::InputStream::Stdin, terminalInputCapabilities());
        Hooks::setInputModeOverride(Terminal::Types::InputStream::Stdin, true, true, true);
        setupCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Hooks::setTerminalSizeOverride(Terminal::Types::OutputStream::Stdout, {.columns = 100, .rows = 30});
        Hooks::setCursorPositionOverride(Terminal::Types::OutputStream::Stdout, {.column = 3, .row = 4});

        Terminal::Session closed;
        static_cast<void>(context.expectFalse("default session starts closed", closed.isOpen()));
        static_cast<void>(context.expectEq("closed session read reports NotOpen", ErrorCode::NotOpen, closed.readText().status.code));
        static_cast<void>(
            context.expectEq("closed session input query reports NotOpen", ErrorCode::NotOpen, closed.getInputCapabilities().status.code));
        static_cast<void>(context.expectEq("closed session write reports NotOpen", ErrorCode::NotOpen, closed.writeText("closed").code));
        static_cast<void>(
            context.expectEq("closed session output query reports NotOpen", ErrorCode::NotOpen, closed.getOutputCapabilities().status.code));
        static_cast<void>(context.expectEq("closed session prepare reports NotOpen", ErrorCode::NotOpen, closed.prepareOutput().status.code));
        static_cast<void>(context.expectEq("closed session size reports NotOpen", ErrorCode::NotOpen, closed.getTerminalSize().status.code));
        static_cast<void>(context.expectEq("closed session line write reports NotOpen", ErrorCode::NotOpen, closed.writeLine().code));
        static_cast<void>(context.expectEq("closed session byte write reports NotOpen", ErrorCode::NotOpen, closed.writeBytes({}).status.code));
        static_cast<void>(context.expectEq("closed session segment write reports NotOpen", ErrorCode::NotOpen, closed.writeSegments({}).code));
        static_cast<void>(context.expectEq("closed session print reports NotOpen", ErrorCode::NotOpen, closed.print("{}", 1).code));
        static_cast<void>(context.expectEq("closed session println reports NotOpen", ErrorCode::NotOpen, closed.println("{}", 1).code));
        static_cast<void>(context.expectEq("closed session flush reports NotOpen", ErrorCode::NotOpen, closed.flush().code));
        static_cast<void>(context.expectEq("closed session style reports NotOpen", ErrorCode::NotOpen, closed.resetStyle().code));
        static_cast<void>(context.expectEq(
            "closed session cursor move reports NotOpen",
            ErrorCode::NotOpen,
            closed.moveCursor(Terminal::Types::CursorMoveDirection::Up).code));
        static_cast<void>(context.expectEq("closed session cursor set reports NotOpen", ErrorCode::NotOpen, closed.setCursorPosition({}).code));
        static_cast<void>(
            context.expectEq("closed session cursor query reports NotOpen", ErrorCode::NotOpen, closed.getCursorPosition().status.code));
        static_cast<void>(context.expectEq("closed session cursor save reports NotOpen", ErrorCode::NotOpen, closed.saveCursorPosition().code));
        static_cast<void>(context.expectEq("closed session cursor restore reports NotOpen", ErrorCode::NotOpen, closed.restoreCursorPosition().code));
        static_cast<void>(context.expectEq("closed session visibility reports NotOpen", ErrorCode::NotOpen, closed.setCursorVisible(false).code));
        static_cast<void>(context.expectEq("closed session clear reports NotOpen", ErrorCode::NotOpen, closed.clear().code));
        static_cast<void>(
            context.expectEq("closed session scroll reports NotOpen", ErrorCode::NotOpen, closed.scroll(Terminal::Types::ScrollDirection::Up).code));
        static_cast<void>(context.expectEq("closed session alternate enter reports NotOpen", ErrorCode::NotOpen, closed.enterAlternateScreen().code));
        static_cast<void>(context.expectEq("closed session alternate leave reports NotOpen", ErrorCode::NotOpen, closed.leaveAlternateScreen().code));
        static_cast<void>(context.expectEq("closed session title reports NotOpen", ErrorCode::NotOpen, closed.setTitle("closed").code));
        static_cast<void>(context.expectEq("closed session bell reports NotOpen", ErrorCode::NotOpen, closed.ringBell().code));
        static_cast<void>(context.expectTrue("closing a closed session is idempotent", closed.close().ok()));

        Terminal::Types::SessionOptions streamOptions;
        streamOptions.deliveryMode = Terminal::Types::InputDeliveryMode::Stream;

        Terminal::Session session;
        static_cast<void>(context.expectTrue("stream session opens", session.open(streamOptions).ok()));
        static_cast<void>(context.expectTrue("opened session reports open", session.isOpen()));
        static_cast<void>(context.expectTrue(
            "stream session uses immediate managed input mode",
            Hooks::inputModeOverrideMatches(Terminal::Types::InputStream::Stdin, false, false, true)));
        static_cast<void>(context.expectTrue(
            "stream session enables resize records without mouse/Quick Edit",
            Hooks::inputManagedEventModeOverrideMatches(Terminal::Types::InputStream::Stdin, true, false, true)));

        const Terminal::Types::InputCapabilitiesResult sessionInputCapabilities = session.getInputCapabilities();
        static_cast<void>(context.expectTrue("session input capability query succeeds", sessionInputCapabilities.status.ok()));
        static_cast<void>(context.expectTrue("session reuses captured event capability", sessionInputCapabilities.capabilities.supportsEventInput));

        const Terminal::Types::OutputCapabilitiesResult sessionOutputCapabilities = session.getOutputCapabilities();
        static_cast<void>(context.expectTrue("session output capability query succeeds", sessionOutputCapabilities.status.ok()));
        static_cast<void>(context.expectTrue("session output capability is bound stdout", sessionOutputCapabilities.capabilities.supportsUtf8Text));
        static_cast<void>(context.expectTrue("session output preparation succeeds", session.prepareOutput().status.ok()));

        const Terminal::Types::TerminalSizeResult sessionSize = session.getTerminalSize();
        static_cast<void>(context.expectTrue("session size query succeeds", sessionSize.status.ok()));
        static_cast<void>(context.expectEq("session size query uses bound output", std::uint32_t{100}, sessionSize.size.columns));

        const Terminal::Types::CursorPositionResult sessionPosition =
            session.getCursorPosition({.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None});
        static_cast<void>(context.expectTrue("session cursor position query succeeds", sessionPosition.status.ok()));
        static_cast<void>(context.expectEq("session cursor query uses owned input", std::uint32_t{3}, sessionPosition.position.column));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Types::LineWriteOptions sessionLineOptions;
        sessionLineOptions.lineEnding = Terminal::Types::LineEnding::Lf;
        static_cast<void>(context.expectTrue("session text write succeeds", session.writeText("session").ok()));
        static_cast<void>(context.expectTrue("session line write succeeds", session.writeLine("-line", sessionLineOptions).ok()));
        static_cast<void>(context.expectTrue("session print succeeds", session.print("-{}", 7).ok()));
        static_cast<void>(context.expectTrue("session println succeeds", session.println(sessionLineOptions, "-{}", 8).ok()));

        const std::string sessionBytesText = "!";
        const IO::Types::WriteResult sessionBytes = session.writeBytes(bytesOf(sessionBytesText));
        static_cast<void>(context.expectTrue("session byte write succeeds", sessionBytes.status.ok()));
        static_cast<void>(context.expectEq("session byte write count", std::size_t{1}, sessionBytes.bytesWritten));

        const std::array<Terminal::Types::WriteSegment, 2> sessionSegments{Terminal::textSegment("seg"), Terminal::textSegment("ment")};
        static_cast<void>(context.expectTrue(
            "session segmented write succeeds",
            session.writeSegments(std::span<const Terminal::Types::WriteSegment>(sessionSegments)).ok()));
        static_cast<void>(context.expectTrue("direct global output remains available while session owns stdin", Terminal::writeText("-global").ok()));
        static_cast<void>(context.expectEq(
            "session output shares global serialization implementation",
            std::string{"session-line\n-7-8\n!segment-global"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("session reset style succeeds", session.resetStyle().ok()));
        static_cast<void>(context.expectTrue("session cursor move succeeds", session.moveCursor(Terminal::Types::CursorMoveDirection::Up, 2).ok()));
        static_cast<void>(context.expectTrue("session cursor set succeeds", session.setCursorPosition({.column = 4, .row = 2}).ok()));
        static_cast<void>(context.expectTrue("session cursor save succeeds", session.saveCursorPosition().ok()));
        static_cast<void>(context.expectTrue("session cursor restore succeeds", session.restoreCursorPosition().ok()));
        static_cast<void>(context.expectTrue("session clear succeeds", session.clear(Terminal::Types::ClearTarget::LineAfterCursor).ok()));
        static_cast<void>(context.expectTrue("session scroll succeeds", session.scroll(Terminal::Types::ScrollDirection::Up, 1).ok()));
        static_cast<void>(context.expectTrue("session title succeeds", session.setTitle("Session").ok()));
        static_cast<void>(context.expectTrue("session bell succeeds", session.ringBell().ok()));
        static_cast<void>(context.expectTrue("session flush succeeds", session.flush(IO::Types::FlushMode::Data).ok()));

        static_cast<void>(context.expectEq("same session re-open reports AlreadyOpen", ErrorCode::AlreadyOpen, session.open(streamOptions).code));

        Terminal::Session competing;
        static_cast<void>(context.expectEq("competing session reports ResourceBusy", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
        static_cast<void>(
            context.expectEq("direct read conflicts with session ownership", ErrorCode::ResourceBusy, Terminal::readText().status.code));

        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, "session");
        const Terminal::Types::TextReadResult sessionText = session.readText();
        static_cast<void>(context.expectTrue("session text read succeeds", sessionText.status.ok()));
        static_cast<void>(context.expectEq("session text read payload", std::string{"session"}, sessionText.text));

        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, "preserved");
        static_cast<void>(context.expectEq(
            "stream session rejects event consumer",
            ErrorCode::Unsupported,
            session.readEvent({.timeout = Terminal::kNoWait}).status.code));
        static_cast<void>(context.expectEq("incompatible event read consumes nothing", std::string{"preserved"}, session.readText().text));

        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, "deadline");
        Terminal::Types::TextReadOptions negativeTimeout;
        negativeTimeout.timeout = std::chrono::milliseconds{-1};
        static_cast<void>(
            context.expectEq("negative read deadline is rejected", ErrorCode::InvalidArgument, session.readText(negativeTimeout).status.code));
        static_cast<void>(context.expectEq("negative deadline consumes nothing", std::string{"deadline"}, session.readText().text));

        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, "cancelled");
        std::stop_source stopSource;
        stopSource.request_stop();
        Terminal::Types::TextReadOptions cancelledOptions;
        cancelledOptions.stopToken = stopSource.get_token();
        const Terminal::Types::TextReadResult cancelled = session.readText(cancelledOptions);
        static_cast<void>(context.expectTrue("pre-cancelled read keeps success status", cancelled.status.ok()));
        static_cast<void>(context.expectEq("pre-cancelled read outcome", Terminal::Types::ReadOutcome::Cancelled, cancelled.outcome));
        static_cast<void>(context.expectEq("pre-cancelled read consumes nothing", std::string{"cancelled"}, session.readText().text));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("session owns hidden cursor state", session.setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("session owns alternate-screen state", session.enterAlternateScreen().ok()));

        Hooks::forceNextInputModeFailure(ErrorCode::NativeFailure);
        static_cast<void>(context.expectEq("session input restoration failure propagates", ErrorCode::NativeFailure, session.close().code));
        static_cast<void>(context.expectTrue("failed close leaves session open", session.isOpen()));
        static_cast<void>(context.expectEq(
            "session restores persistent output in reverse order before input",
            std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectEq("failed close retains ownership", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
        static_cast<void>(context.expectTrue("session close retry succeeds", session.close().ok()));
        static_cast<void>(context.expectFalse("successful close clears open state", session.isOpen()));
        static_cast<void>(context.expectTrue(
            "successful close restores exact input mode",
            Hooks::inputModeOverrideMatches(Terminal::Types::InputStream::Stdin, true, true, true)));
        static_cast<void>(context.expectTrue(
            "successful close restores exact managed-event flags",
            Hooks::inputManagedEventModeOverrideMatches(Terminal::Types::InputStream::Stdin, false, false, false)));
        static_cast<void>(context.expectEq("session output after close reports NotOpen", ErrorCode::NotOpen, session.writeText("closed").code));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Session duplicateOutputState;
        static_cast<void>(context.expectTrue("duplicate output-state fixture opens", duplicateOutputState.open(streamOptions).ok()));
        static_cast<void>(context.expectTrue("duplicate output-state fixture hides cursor", duplicateOutputState.setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("duplicate cursor hide is idempotent", duplicateOutputState.setCursorVisible(false).ok()));
        static_cast<void>(
            context.expectTrue("duplicate output-state fixture enters alternate screen", duplicateOutputState.enterAlternateScreen().ok()));
        static_cast<void>(context.expectTrue("duplicate alternate enter is idempotent", duplicateOutputState.enterAlternateScreen().ok()));
        static_cast<void>(context.expectTrue("explicit cursor show removes obligation", duplicateOutputState.setCursorVisible(true).ok()));
        static_cast<void>(context.expectTrue("explicit alternate leave removes obligation", duplicateOutputState.leaveAlternateScreen().ok()));
        static_cast<void>(context.expectTrue("closing after explicit inverses succeeds", duplicateOutputState.close().ok()));
        static_cast<void>(context.expectEq(
            "duplicate state changes emit once and explicit inverses are not repeated by close",
            std::string{"\x1b[?25l\x1b[?1049h\x1b[?25h\x1b[?1049l"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Session setupFlushFailure;
        static_cast<void>(context.expectTrue("setup flush failure fixture opens", setupFlushFailure.open(streamOptions).ok()));
        Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
        static_cast<void>(context.expectEq(
            "Session cursor setup flush failure propagates",
            ErrorCode::FlushFailed,
            setupFlushFailure.setCursorVisible(false, {.flushMode = IO::Types::FlushMode::Data}).code));
        static_cast<void>(context.expectTrue("Session retains cleanup after setup flush failure", setupFlushFailure.close().ok()));
        static_cast<void>(context.expectEq(
            "Session setup flush failure does not lose cleanup ownership",
            std::string{"\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Session outputRestoreFailure;
        static_cast<void>(context.expectTrue("output restoration failure fixture opens", outputRestoreFailure.open(streamOptions).ok()));
        static_cast<void>(context.expectTrue("output restoration fixture hides cursor", outputRestoreFailure.setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("output restoration fixture enters alternate screen", outputRestoreFailure.enterAlternateScreen().ok()));
        Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
        static_cast<void>(
            context.expectEq("output restoration failure propagates from close", ErrorCode::WriteFailed, outputRestoreFailure.close().code));
        static_cast<void>(context.expectTrue("output restoration failure leaves session open", outputRestoreFailure.isOpen()));
        static_cast<void>(context.expectEq(
            "failed top restoration does not run older obligations",
            std::string{"\x1b[?25l\x1b[?1049h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(
            context.expectEq("output restoration failure retains input ownership", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
        static_cast<void>(context.expectTrue("output restoration close retry succeeds", outputRestoreFailure.close().ok()));
        static_cast<void>(context.expectEq(
            "output restoration retry preserves reverse order",
            std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Terminal::Session flushRestoreFailure;
        static_cast<void>(context.expectTrue("restoration flush failure fixture opens", flushRestoreFailure.open(streamOptions).ok()));
        const Terminal::Types::ControlOptions flushedControl{.flushMode = IO::Types::FlushMode::Data};
        static_cast<void>(
            context.expectTrue("restoration flush fixture hides cursor", flushRestoreFailure.setCursorVisible(false, flushedControl).ok()));
        Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
        static_cast<void>(context.expectEq("close restoration flush failure propagates", ErrorCode::FlushFailed, flushRestoreFailure.close().code));
        static_cast<void>(context.expectTrue("restoration flush failure leaves session open", flushRestoreFailure.isOpen()));
        static_cast<void>(context.expectEq(
            "restoration transition was emitted before flush failure",
            std::string{"\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectTrue("restoration flush retry closes session", flushRestoreFailure.close().ok()));
        static_cast<void>(context.expectEq(
            "restoration flush retry does not repeat completed transition",
            std::string{"\x1b[?25l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        setupCapturedOutput(Terminal::Types::OutputStream::Stderr);
        Hooks::setTerminalSizeOverride(Terminal::Types::OutputStream::Stderr, {.columns = 72, .rows = 20});
        Hooks::setCursorPositionOverride(Terminal::Types::OutputStream::Stderr, {.column = 11, .row = 6});
        Terminal::Types::SessionOptions stderrOptions = streamOptions;
        stderrOptions.output = Terminal::Types::OutputStream::Stderr;
        Terminal::Session stderrSession;
        static_cast<void>(context.expectTrue("stderr-bound session opens", stderrSession.open(stderrOptions).ok()));
        static_cast<void>(context.expectTrue("stderr-bound session write succeeds", stderrSession.writeText("bound-stderr").ok()));
        static_cast<void>(context.expectEq(
            "session output is bound to requested stream",
            std::string{"bound-stderr"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stderr)));
        static_cast<void>(context.expectEq("stderr-bound size query", std::uint32_t{72}, stderrSession.getTerminalSize().size.columns));
        static_cast<void>(context.expectEq(
            "stderr-bound cursor query",
            std::uint32_t{11},
            stderrSession.getCursorPosition({.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None}).position.column));
        static_cast<void>(context.expectTrue("stderr-bound session closes", stderrSession.close().ok()));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Hooks::setInputBytes(Terminal::Types::InputStream::Stdin, "blocked-read");
        Terminal::Session concurrencySession;
        static_cast<void>(context.expectTrue("concurrency fixture opens", concurrencySession.open(streamOptions).ok()));

        Hooks::blockNextTextWrite();
        IO::Types::Status sessionSerializedWrite;
        IO::Types::Status globalSerializedWrite;
        std::jthread sessionWriter(
            [&]
            {
                sessionSerializedWrite = concurrencySession.writeText("session-first");
            });
        const bool writeReachedBlock = Hooks::waitUntilTextWriteBlocked();
        static_cast<void>(context.expectTrue("session write reaches deterministic backend gate", writeReachedBlock));
        std::jthread globalWriter(
            [&]
            {
                globalSerializedWrite = Terminal::writeText("-global-second");
            });
        Hooks::releaseBlockedTextWrite();
        sessionWriter.join();
        globalWriter.join();
        static_cast<void>(context.expectTrue("serialized session write succeeds", sessionSerializedWrite.ok()));
        static_cast<void>(context.expectTrue("serialized global write succeeds", globalSerializedWrite.ok()));
        static_cast<void>(context.expectEq(
            "Session and global writes use one stream serialization domain",
            std::string{"session-first-global-second"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        std::latch formatterEntered{1};
        std::latch releaseFormatter{1};
        IO::Types::Status blockedFormatStatus;
        std::jthread formatterThread(
            [&]
            {
                blockedFormatStatus =
                    concurrencySession.print("{}", TerminalSessionBlockingFormat{.entered = &formatterEntered, .release = &releaseFormatter});
            });
        formatterEntered.wait();

        std::atomic_bool formattingCloseFinished = false;
        std::latch formattingCloseStarted{1};
        IO::Types::Status formattingCloseStatus;
        std::jthread formattingCloser(
            [&]
            {
                formattingCloseStarted.count_down();
                formattingCloseStatus = concurrencySession.close();
                formattingCloseFinished.store(true, std::memory_order_release);
            });
        formattingCloseStarted.wait();
        static_cast<void>(context.expectFalse(
            "close waits while a Session formatter owns an active operation",
            formattingCloseFinished.load(std::memory_order_acquire)));
        releaseFormatter.count_down();
        formatterThread.join();
        formattingCloser.join();
        static_cast<void>(context.expectTrue("blocked Session formatting completes", blockedFormatStatus.ok()));
        static_cast<void>(context.expectTrue("close completes after Session formatting", formattingCloseStatus.ok()));
        static_cast<void>(context.expectEq(
            "blocked Session formatter emits its complete outer record",
            std::string{"blocked-format"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        static_cast<void>(context.expectTrue("concurrency fixture reopens after formatting close", concurrencySession.open(streamOptions).ok()));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        Hooks::blockNextRead();
        Terminal::Types::TextReadResult blockedRead;
        std::jthread reader(
            [&]
            {
                blockedRead = concurrencySession.readText();
            });
        const bool readReachedBlock = Hooks::waitUntilReadBlocked();
        static_cast<void>(context.expectTrue("blocking read reaches deterministic backend gate", readReachedBlock));
        static_cast<void>(
            context.expectTrue("session output remains usable during blocked session input", concurrencySession.writeText("during-read").ok()));
        static_cast<void>(context.expectEq(
            "output during blocked read is emitted immediately",
            std::string{"during-read"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        std::atomic_bool closeFinished = false;
        std::latch closeStarted{1};
        IO::Types::Status concurrentCloseStatus;
        std::jthread closer(
            [&]
            {
                closeStarted.count_down();
                concurrentCloseStatus = concurrencySession.close();
                closeFinished.store(true, std::memory_order_release);
            });
        closeStarted.wait();
        static_cast<void>(
            context.expectFalse("close waits while a session read owns shared lifecycle access", closeFinished.load(std::memory_order_acquire)));
        Hooks::releaseBlockedRead();
        reader.join();
        closer.join();
        static_cast<void>(context.expectTrue("blocked read completes after release", blockedRead.status.ok()));
        static_cast<void>(context.expectEq("blocked read payload", std::string{"blocked-read"}, blockedRead.text));
        static_cast<void>(context.expectTrue("close completes after active read", concurrentCloseStatus.ok()));
        static_cast<void>(context.expectFalse("concurrency fixture is closed", concurrencySession.isOpen()));

        Terminal::Session movable;
        static_cast<void>(context.expectTrue("movable session opens", movable.open(streamOptions).ok()));
        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        static_cast<void>(context.expectTrue("movable session hides cursor", movable.setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("movable session enters alternate screen", movable.enterAlternateScreen().ok()));
        Terminal::Session moved(std::move(movable));
        // NOLINTNEXTLINE(bugprone-use-after-move) -- Session explicitly specifies a closed, queryable moved-from state.
        static_cast<void>(context.expectFalse("moved-from session becomes closed", movable.isOpen()));
        static_cast<void>(context.expectTrue("move construction preserves open ownership", moved.isOpen()));
        static_cast<void>(context.expectEq("moved session still blocks competitors", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
        static_cast<void>(context.expectTrue("moved session closes", moved.close().ok()));
        static_cast<void>(context.expectEq(
            "move construction preserves pending output cleanup",
            std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::Session scoped;
            static_cast<void>(context.expectTrue("destructor fixture opens", scoped.open(streamOptions).ok()));
            static_cast<void>(context.expectTrue("destructor fixture hides cursor", scoped.setCursorVisible(false).ok()));
            static_cast<void>(context.expectTrue("destructor fixture enters alternate screen", scoped.enterAlternateScreen().ok()));
            static_cast<void>(context.expectTrue(
                "destructor fixture owns immediate stream mode",
                Hooks::inputModeOverrideMatches(Terminal::Types::InputStream::Stdin, false, false, true)));
        }
        static_cast<void>(context.expectTrue(
            "session destructor restores mode",
            Hooks::inputModeOverrideMatches(Terminal::Types::InputStream::Stdin, true, true, true)));
        static_cast<void>(context.expectEq(
            "session destructor restores pending output state",
            std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::Session failedDestructor;
            static_cast<void>(context.expectTrue("failed destructor fixture opens", failedDestructor.open(streamOptions).ok()));
            static_cast<void>(context.expectTrue("failed destructor fixture hides cursor", failedDestructor.setCursorVisible(false).ok()));
            static_cast<void>(context.expectTrue("failed destructor fixture enters alternate screen", failedDestructor.enterAlternateScreen().ok()));
            Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
        }
        static_cast<void>(context.expectTrue(
            "failed Session destructor still restores input mode",
            Hooks::inputModeOverrideMatches(Terminal::Types::InputStream::Stdin, true, true, true)));
        static_cast<void>(context.expectEq(
            "failed Session destructor does not retry out of reverse order",
            std::string{"\x1b[?25l\x1b[?1049h\x1b[?25h"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));
        Hooks::clearCapturedOutput(Terminal::Types::OutputStream::Stdout);
        {
            Terminal::AlternateScreenScope laterAlternateScope = Terminal::scopedAlternateScreen();
            static_cast<void>(
                context.expectTrue("alternate scope nesting remains usable after Session destructor failure", laterAlternateScope.leave().ok()));
        }
        static_cast<void>(context.expectEq(
            "Session destructor failure releases stale alternate nesting ownership",
            std::string{"\x1b[?1049h\x1b[?1049l"},
            Hooks::capturedOutputText(Terminal::Types::OutputStream::Stdout)));

        Terminal::Session events;
        static_cast<void>(context.expectTrue("default event session opens with event-capable hook", events.open().ok()));
        static_cast<void>(context.expectEq("event session rejects stream reads", ErrorCode::Unsupported, events.readText().status.code));

        std::stop_source eventStopSource;
        eventStopSource.request_stop();
        Terminal::Types::EventReadOptions eventCancelledOptions;
        eventCancelledOptions.stopToken = eventStopSource.get_token();
        const Terminal::Types::EventReadResult cancelledEvent = events.readEvent(eventCancelledOptions);
        static_cast<void>(context.expectTrue("event cancellation keeps success status", cancelledEvent.status.ok()));
        static_cast<void>(context.expectEq("event cancellation outcome", Terminal::Types::ReadOutcome::Cancelled, cancelledEvent.outcome));
        static_cast<void>(context.expectTrue("event session closes", events.close().ok()));

        Terminal::Types::SessionOptions reportControlOptions = streamOptions;
        reportControlOptions.controlKeyMode = Terminal::Types::ControlKeyMode::ReportAsInput;
        Terminal::Session reportControl;
        static_cast<void>(context.expectTrue("report-as-input session opens", reportControl.open(reportControlOptions).ok()));
        static_cast<void>(context.expectTrue(
            "report-as-input disables native control processing",
            Hooks::inputModeOverrideMatches(Terminal::Types::InputStream::Stdin, false, false, false)));
        static_cast<void>(context.expectTrue("report-as-input session closes", reportControl.close().ok()));

        Terminal::Types::SessionOptions invalidOptions = streamOptions;
        invalidOptions.deliveryMode = static_cast<Terminal::Types::InputDeliveryMode>(99);
        static_cast<void>(
            context.expectEq("invalid session delivery mode is rejected", ErrorCode::InvalidArgument, competing.open(invalidOptions).code));

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
        if (hasArgument(argc, argv, kSessionReentrantFormatChildArgument))
        {
            return runSessionReentrantFormatChild();
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
        runner.runSuite("Terminal managed line editing", testManagedLineEditing);
#if defined(_WIN32)
        runner.runSuite("Terminal Win32 event decoder", testWin32EventDecoder);
        runner.runSuite("Terminal stdin endpoint replacement", testInputEndpointReplacement);
#endif
        runner.runSuite("Terminal sessions and ownership", testSessions);
#else
        runner.runSuite("Terminal hook-dependent suites", testHookDependentSuitesSkipped);
#endif

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Terminal library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
