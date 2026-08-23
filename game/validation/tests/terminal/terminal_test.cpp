/// @file terminal_test.cpp
/// @brief Executable self-tests for the Terminal library.
///
/// The suite combines backend hooks, redirected and console-like endpoints,
/// reentrant child execution, state restoration, and opt-in manual tests.

#include "validation/tests/terminal/terminal_test.h"

#include "terminal/terminal.h"
#include "test_support/test_support.h"

#ifndef TERMINAL_INTERNAL_TEST_HOOKS
#define TERMINAL_INTERNAL_TEST_HOOKS 0
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
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

    static_assert(!std::is_aggregate_v<Terminal::Types::Style::Color>);
    static_assert(!std::is_aggregate_v<Terminal::Types::Output::Segment>);

    template <typename Text>
    concept CanCreateTextSegment = requires(Text &&text) { Terminal::textSegment(std::forward<Text>(text)); };

    template <typename Text>
    concept CanCreateStyledTextSegment =
        requires(Text &&text, const Terminal::Types::Style::Request &style) { Terminal::styledTextSegment(std::forward<Text>(text), style); };

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

        Terminal::Types::Output::LineOptions options;
        options.lineEnding = Terminal::Types::Output::LineEnding::Lf;
        return Terminal::println(options, "{}", TerminalReentrantFormat{}).ok() ? 0 : 1;
    }

    /// @brief Exercises Session formatter reentry and same-operation close behavior in an isolated child.
    [[nodiscard]] int runSessionReentrantFormatChild()
    {
        Terminal::Types::SessionOptions sessionOptions;
        sessionOptions.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;

        Terminal::Session session;
        if (!session.open(sessionOptions).ok())
        {
            return 1;
        }
        if (!session.print("{}", TerminalSessionReentrantFormat{.session = &session}).ok())
        {
            return 2;
        }

        Terminal::Types::Output::LineOptions lineOptions;
        lineOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
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

    /// @brief Records a human response as a test pass, failure, or skip.
    void recordManualAnswer(TestSupport::Context &context, std::string_view name, TestSupport::Types::Reporting::ManualAnswer answer)
    {
        switch (answer)
        {
        case TestSupport::Types::Reporting::ManualAnswer::Yes:
            context.pass(name);
            return;

        case TestSupport::Types::Reporting::ManualAnswer::No:
            context.fail(name, "manual check rejected by user");
            return;

        case TestSupport::Types::Reporting::ManualAnswer::Skipped:
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

#if TERMINAL_INTERNAL_TEST_HOOKS
    namespace Hooks = GameWIP::Terminal::TestHooks;

    /// @brief Returns a fixture that enables every style capability.
    [[nodiscard]] Terminal::Types::Style::Capabilities allStyleCapabilities() noexcept
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
    [[nodiscard]] Terminal::Types::Output::Capabilities terminalOutputCapabilities() noexcept
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
    [[nodiscard]] Terminal::Types::Output::Capabilities redirectedOutputCapabilities() noexcept
    {
        return {.kind = Terminal::Types::StreamKind::Redirected, .supportsUtf8Text = true, .supportsByteOutput = true, .supportsFlush = true};
    }

    /// @brief Returns interactive-output capabilities before virtual-terminal preparation.
    [[nodiscard]] Terminal::Types::Output::Capabilities unpreparedTerminalOutputCapabilities() noexcept
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
    [[nodiscard]] Terminal::Types::Input::Capabilities terminalInputCapabilities() noexcept
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
    [[nodiscard]] Terminal::Types::Input::Capabilities redirectedInputCapabilities() noexcept
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
    void setupCapturedOutput(
        Terminal::Types::Output::Stream stream,
        Terminal::Types::Output::Capabilities capabilities = terminalOutputCapabilities())
    {
        Hooks::setOutputCapabilitiesOverride(stream, capabilities);
        Hooks::setOutputCapture(stream, true);
        Hooks::clearCapturedOutput(stream);
    }

    /// @brief Resets hooks and installs deterministic stdin bytes and EOF policy.
    void setupInput(std::string_view bytes, bool endOfStreamWhenEmpty = true)
    {
        Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, redirectedInputCapabilities());
        Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, bytes, endOfStreamWhenEmpty);
    }

#endif

    // Focused suite declarations keep cross-suite calls independent of fragment include order.
    void testReentrantFormatting(TestSupport::Context &context, std::string_view executablePath);
    void testManualUnicodeOutput(TestSupport::Context &context);
    void testManualBasicColorOutput(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities);
    void testManualRgbColorOutput(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities);
    void testManualTextStyles(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities);
    void testManualStyleRestoration(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities);
    void testManualCursorBehavior(TestSupport::Context &context, const Terminal::Types::Output::Capabilities &capabilities);
    void testManualAlternateScreen(TestSupport::Context &context, const Terminal::Types::Output::Capabilities &capabilities);
    void testManualInput(TestSupport::Context &context);
    void testManualWrappedLineEditing(TestSupport::Context &context);
    void testManualEventInput(TestSupport::Context &context, const Terminal::Types::Input::Capabilities &inputCapabilities);
    void testManualStateRestoration(
        TestSupport::Context &context,
        const Terminal::Types::Input::Capabilities &inputCapabilities,
        const Terminal::Types::Output::Capabilities &outputCapabilities);
    void testManualUiChecks(TestSupport::Context &context, const TerminalTestOptions &options);
    void testPassiveHelpers(TestSupport::Context &context);
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testCapabilitiesAndQueries(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testUtf8OutputContracts(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testTextAndStyleOutput(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testSegmentedAndByteOutput(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testControls(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testInputReads(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
#if defined(_WIN32)
    void testWin32EventDecoder(TestSupport::Context &context);
#endif
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
#if defined(_WIN32)
    void testInputEndpointReplacement(TestSupport::Context &context);
#endif
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testManagedLineEditing(TestSupport::Context &context);
#endif
#if TERMINAL_INTERNAL_TEST_HOOKS
    void testSessions(TestSupport::Context &context);
#endif
#if !TERMINAL_INTERNAL_TEST_HOOKS
    void testHookDependentSuitesSkipped(TestSupport::Context &context);
#endif

#include "validation/tests/terminal/contracts_test.inl"
#include "validation/tests/terminal/controls_test.inl"
#include "validation/tests/terminal/input_test.inl"
#include "validation/tests/terminal/line_input_test.inl"
#include "validation/tests/terminal/manual_test.inl"
#include "validation/tests/terminal/output_test.inl"
#include "validation/tests/terminal/session_test.inl"
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

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(
            std::format("Terminal test options: report={}", options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

        runner.runSuite("Terminal passive helpers", testPassiveHelpers);
#if TERMINAL_INTERNAL_TEST_HOOKS
        runner.runSuite("Terminal UTF-8 output contracts", testUtf8OutputContracts);
#endif
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

#if TERMINAL_INTERNAL_TEST_HOOKS
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

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Terminal library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
