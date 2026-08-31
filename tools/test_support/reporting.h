/// @file reporting.h
/// @brief Test reporting, expectations, manual checks, and timing helpers.

#pragma once

#include "test_support/types.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace GameWIP::TestSupport
{
    /// @brief Passive reporting configuration and result values.
    namespace Types::Reporting
    {
        /// @brief Controls which report categories are mirrored to stdout.
        enum class ConsoleVerbosity
        {
            Minimal, ///< Failures, skips, and manual instructions only.
            Concise, ///< Minimal output plus suite results and summaries.
            Full,    ///< Every report category, including passes and diagnostics.
        };

        /// @brief Runtime report-output settings shared by test suites.
        struct Options
        {
            bool writeConsole = true;                                   ///< Mirrors enabled report categories to stdout.
            bool writeReport = true;                                    ///< Enables report-file setup when reportPath is non-empty.
            bool appendReport = false;                                  ///< Appends instead of truncating when the report sink opens.
            bool flushReportEachLine = false;                           ///< Flushes report-file output after each written line.
            ConsoleVerbosity consoleVerbosity = ConsoleVerbosity::Full; ///< Categories mirrored to stdout.
            std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report-file destination.
        };

        /// @brief Pass/fail/skip counts for one context, suite, or run.
        struct Summary
        {
            std::size_t passed = 0;  ///< Number of passing checks or scenarios.
            std::size_t failed = 0;  ///< Number of failing checks or scenarios.
            std::size_t skipped = 0; ///< Number of explicitly skipped checks or scenarios.

            /// @brief Returns passed + failed + skipped.
            [[nodiscard]] std::size_t total() const noexcept;
            /// @brief Returns true when failed is zero, including empty and skipped-only summaries.
            [[nodiscard]] bool ok() const noexcept;
        };

        /// @brief Result of one named test suite run by Runner.
        struct SuiteResult
        {
            std::string name;                 ///< Suite display name.
            Summary summary;                  ///< Suite pass/fail/skip counts.
            double elapsedMilliseconds = 0.0; ///< Wall-clock suite duration in milliseconds.

            /// @brief Returns summary.ok().
            [[nodiscard]] bool ok() const noexcept;
        };

        /// @brief Answer selected by a human for a manual check.
        enum class ManualAnswer
        {
            Yes,    ///< The user accepted or confirmed the manual check.
            No,     ///< The user rejected or failed the manual check.
            Skipped ///< The user skipped the check or input ended before an answer.
        };
    } // namespace Types::Reporting

    /// @cond INTERNAL_TEST_SUPPORT
    namespace Detail
    {
        class ReportSink;

        template <typename Value> [[nodiscard]] std::string valueToString(const Value &value)
        {
            if constexpr (std::is_same_v<std::remove_cvref_t<Value>, bool>)
            {
                return value ? "true" : "false";
            }
            else if constexpr (requires(std::ostream &stream, const Value &streamValue) { stream << streamValue; })
            {
                std::ostringstream stream;
                stream << value;
                return stream.str();
            }
            else
            {
                return "<unprintable>";
            }
        }
    } // namespace Detail
    /// @endcond

    /// @brief Monotonic elapsed-time helper for test diagnostics.
    /// @note Timer uses std::chrono::steady_clock and is not benchmark-grade measurement.
    class Timer
    {
    public:
        /// @brief Starts elapsed-time measurement at construction.
        Timer() noexcept;
        /// @brief Restarts elapsed-time measurement from now.
        void reset() noexcept;
        /// @brief Returns elapsed wall-clock time in milliseconds.
        [[nodiscard]] double elapsedMilliseconds() const noexcept;

    private:
        using Clock = std::chrono::steady_clock;
        Clock::time_point start_; ///< Current measurement origin.
    };

    /// @brief Test context that records outcomes and routes categorized report lines.
    class Context
    {
    public:
        /// @brief Creates a standalone context with its own report sink.
        Context(std::string_view suiteName, const Types::Reporting::Options &options);
        /// @brief Releases this context and its shared report-sink ownership.
        ~Context();

        Context(const Context &) = delete;            ///< Context synchronization/sink ownership cannot be copied.
        Context &operator=(const Context &) = delete; ///< Context synchronization/sink ownership cannot be copy-assigned.

        /// @brief Writes an informational line without changing result counts.
        void info(std::string_view message);
        /// @brief Writes a manual-check instruction without changing result counts.
        void manual(std::string_view instruction);
        /// @brief Writes a metric line without changing result counts.
        void metric(std::string_view message);
        /// @brief Writes a stress-test diagnostic without changing result counts.
        void stress(std::string_view message);
        /// @brief Writes a summary line without changing result counts.
        void summary(std::string_view message);

        /// @brief Records one passing check or scenario.
        void pass(std::string_view name);
        /// @brief Records one failing check or scenario with source-location details.
        void fail(std::string_view name, std::string_view reason, std::source_location location = std::source_location::current());
        /// @brief Records one skipped check or scenario.
        void skip(std::string_view name, std::string_view reason);

        /// @brief Expects value to be true and records exactly one pass or failure.
        [[nodiscard]] bool expectTrue(std::string_view name, bool value, std::source_location location = std::source_location::current());
        /// @brief Expects value to be false and records exactly one pass or failure.
        [[nodiscard]] bool expectFalse(std::string_view name, bool value, std::source_location location = std::source_location::current());

        /// @brief Expects expected and actual to compare equal.
        template <typename Expected, typename Actual>
        [[nodiscard]] bool expectEq(
            std::string_view name,
            const Expected &expected,
            const Actual &actual,
            std::source_location location = std::source_location::current());

        /// @brief Expects unexpected and actual to compare not equal.
        template <typename Unexpected, typename Actual>
        [[nodiscard]] bool expectNe(
            std::string_view name,
            const Unexpected &unexpected,
            const Actual &actual,
            std::source_location location = std::source_location::current());

        /// @brief Expects actual to be within non-negative absolute tolerance of expected.
        [[nodiscard]] bool expectNear(
            std::string_view name,
            double expected,
            double actual,
            double tolerance,
            std::source_location location = std::source_location::current());

        /// @brief Expects text to contain expectedSubstring.
        [[nodiscard]] bool expectContains(
            std::string_view name,
            std::string_view text,
            std::string_view expectedSubstring,
            std::source_location location = std::source_location::current());

        /// @brief Expects a strict UTF-8 text file to contain expectedSubstring.
        [[nodiscard]] bool expectFileContains(
            std::string_view name,
            const std::filesystem::path &path,
            std::string_view expectedSubstring,
            std::source_location location = std::source_location::current());

        /// @brief Expects a strict UTF-8 text file to contain text expectedCount times without overlap.
        [[nodiscard]] bool expectFileOccurrenceCount(
            std::string_view name,
            const std::filesystem::path &path,
            std::string_view text,
            std::size_t expectedCount,
            std::source_location location = std::source_location::current());

        /// @brief Returns the context-owned suite name.
        [[nodiscard]] const std::string &suiteName() const noexcept;
        /// @brief Returns a coherent snapshot of this context's counts.
        [[nodiscard]] Types::Reporting::Summary result() const noexcept;
        /// @brief Returns result().ok().
        [[nodiscard]] bool ok() const noexcept;

    private:
        friend class Runner;

        Context(std::string_view suiteName, std::shared_ptr<Detail::ReportSink> reportSink);

        std::string suiteName_;
        std::shared_ptr<Detail::ReportSink> reportSink_;
        mutable std::mutex mutex_;
        Types::Reporting::Summary summary_;

        void writeLine(std::string_view category, std::string_view message);
        void writeFailureLine(std::string_view name, std::string_view reason, const std::source_location &location);
    };

    /// @brief Runs named suites and aggregates one shared report.
    class Runner
    {
    public:
        /// @brief Creates a runner with one shared report sink.
        explicit Runner(Types::Reporting::Options options);
        /// @brief Releases the runner's ownership of its shared report sink.
        ~Runner();

        Runner(const Runner &) = delete;            ///< Runner ownership cannot be copied.
        Runner &operator=(const Runner &) = delete; ///< Runner ownership cannot be copy-assigned.

        /// @brief Runs a named suite and converts an uncaught suite exception into one failed check.
        template <typename SuiteFunction> Types::Reporting::SuiteResult runSuite(std::string_view suiteName, SuiteFunction &&function);

        /// @brief Writes a run-level informational line.
        void info(std::string_view message);
        /// @brief Writes a run-level summary line.
        void summary(std::string_view message);

        /// @brief Returns aggregate counts across suites that have finished.
        [[nodiscard]] Types::Reporting::Summary result() const noexcept;
        /// @brief Returns result().ok().
        [[nodiscard]] bool ok() const noexcept;
        /// @brief Returns zero when no failures were recorded, otherwise one.
        [[nodiscard]] int exitCode() const noexcept;

    private:
        std::shared_ptr<Detail::ReportSink> reportSink_;
        mutable std::mutex mutex_;
        Types::Reporting::Summary summary_;

        void recordSuiteResult(const Types::Reporting::SuiteResult &result);
    };

    /// @brief RAII helper that reports a named section and its elapsed time.
    class Section
    {
    public:
        /// @brief Begins a named section in context.
        Section(Context &context, std::string_view name);
        /// @brief Attempts to report section duration without propagating destructor failures.
        ~Section() noexcept;

        Section(const Section &) = delete;            ///< The context reference cannot be copied as ownership.
        Section &operator=(const Section &) = delete; ///< The context reference cannot be copy-assigned as ownership.

    private:
        Context &context_;
        std::string name_;
        Timer timer_;
    };

    /// @brief Repeatedly prompts for a recognized yes/no/skip line.
    /// @return Selected answer, or Skipped on end-of-input.
    Types::Reporting::ManualAnswer promptManualCheck(std::string_view question);

    template <typename Expected, typename Actual>
    bool Context::expectEq(std::string_view name, const Expected &expected, const Actual &actual, std::source_location location)
    {
        if (expected == actual)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected " << Detail::valueToString(expected) << ", got " << Detail::valueToString(actual);
        fail(name, reason.str(), location);
        return false;
    }

    template <typename Unexpected, typename Actual>
    bool Context::expectNe(std::string_view name, const Unexpected &unexpected, const Actual &actual, std::source_location location)
    {
        if (unexpected != actual)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "did not expect " << Detail::valueToString(actual);
        fail(name, reason.str(), location);
        return false;
    }

    template <typename SuiteFunction> Types::Reporting::SuiteResult Runner::runSuite(std::string_view suiteName, SuiteFunction &&function)
    {
        Timer timer;
        Context context(suiteName, reportSink_);

        try
        {
            if constexpr (std::is_invocable_v<SuiteFunction, Context &>)
            {
                std::forward<SuiteFunction>(function)(context);
            }
            else if constexpr (std::is_invocable_v<SuiteFunction>)
            {
                std::forward<SuiteFunction>(function)();
            }
            else
            {
                static_assert(std::is_invocable_v<SuiteFunction, Context &>, "SuiteFunction must be invocable with Context& or with no arguments.");
            }
        }
        catch (const std::exception &exception)
        {
            context.fail("uncaught exception", exception.what());
        }
        catch (...)
        {
            context.fail("uncaught exception", "unknown exception");
        }

        Types::Reporting::SuiteResult suiteResult;
        suiteResult.name = std::string(suiteName);
        suiteResult.summary = context.result();
        suiteResult.elapsedMilliseconds = timer.elapsedMilliseconds();
        recordSuiteResult(suiteResult);
        return suiteResult;
    }
} // namespace GameWIP::TestSupport
