/// @file test_support.h
/// @brief Lightweight GameWIP-native helpers for writing executable test suites.
///
/// TestSupport provides reporting, expectations, file helpers, environment guards, child-process
/// helpers, manual prompts, timers, and small stress-test helpers without depending on Logger,
/// Assert, or engine systems.
///
/// Passive configuration/result shapes live in `GameWIP::TestSupport::Types`. Active helpers such
/// as `Context`, `Runner`, `Timer`, and `runChildProcess()` live directly in
/// `GameWIP::TestSupport`. Expectations record failures and keep the suite running.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace GameWIP::TestSupport
{
    /// @cond GAMEWIP_TEST_SUPPORT_DETAIL
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

    /// @name Reporting and result types
    /// @{

    /// @brief Report, suite, child-process, and manual-check types.
    namespace Types
    {
        /// @brief Runtime report-output settings shared by GameWIP test suites.
        /// Contract: these options control only test-run behavior. They do not enable or disable compiled code paths.
        struct ReportOptions
        {
            /// @brief Writes report lines to stdout when true.
            bool writeConsole = true;
            /// @brief Writes report lines to reportPath when true.
            bool writeReport = true;
            /// @brief Appends to reportPath instead of replacing it at the start of a run.
            bool appendReport = false;

            /// @brief Text report path used when writeReport is true.
            std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
        };

        /// @brief Pass/fail/skip counts for one context, suite, or run.
        struct Summary
        {
            /// @brief Number of passing checks or scenarios.
            std::size_t passed = 0;
            /// @brief Number of failing checks or scenarios.
            std::size_t failed = 0;
            /// @brief Number of explicitly skipped checks or scenarios.
            std::size_t skipped = 0;

            /// @brief Returns passed + failed + skipped.
            [[nodiscard]] std::size_t total() const noexcept;
            /// @brief Returns true when failed is zero.
            [[nodiscard]] bool ok() const noexcept;
        };

        /// @brief Result of one named test suite run by Runner.
        struct SuiteResult
        {
            /// @brief Suite display name.
            std::string name;
            /// @brief Suite pass/fail/skip counts.
            Summary summary;
            /// @brief Wall-clock suite duration in milliseconds.
            double elapsedMilliseconds = 0.0;

            /// @brief Returns true when summary.ok() is true.
            [[nodiscard]] bool ok() const noexcept;
        };

        /// @brief Recorded iteration-count metric.
        struct IterationMetric
        {
            /// @brief Metric display name.
            std::string name;
            /// @brief Number of measured iterations.
            std::size_t iterations = 0;
            /// @brief Total measured time in milliseconds.
            double milliseconds = 0.0;

            /// @brief Returns milliseconds converted to nanoseconds per iteration, or zero when iterations is zero.
            [[nodiscard]] double nanosecondsPerIteration() const noexcept;
        };
    } // namespace Types
    /// @}

    /// @name Timing helpers
    /// @{

    /// @brief Monotonic elapsed-time helper for test metrics.
    /// TODO: Revisit this helper when the project adopts a dedicated test or benchmark timing framework.
    class Timer
    {
    public:
        /// @brief Starts the timer at construction.
        Timer() noexcept;

        /// @brief Restarts elapsed-time measurement from now.
        void reset() noexcept;

        /// @brief Returns elapsed wall-clock time in milliseconds.
        [[nodiscard]] double elapsedMilliseconds() const noexcept;
        /// @brief Returns elapsed nanoseconds divided by iterations, or zero when iterations is zero.
        [[nodiscard]] double nanosecondsPerIteration(std::size_t iterations) const noexcept;

    private:
        using Clock = std::chrono::steady_clock;

        Clock::time_point start_;
    };
    /// @}

    /// @name Suite reporting
    /// @{

    /// @brief Test context passed to suite functions.
    /// Thread-safety: public recording methods serialize summary updates and report writes.
    /// Contract: expectations record failures and return false instead of aborting the process.
    /// Failure behavior: report file write failures do not throw from recording methods; console output still continues when enabled.
    class Context
    {
    public:
        /// @brief Creates a standalone context with its own report sink.
        /// @param suiteName Name written into report lines.
        /// @param options Runtime output options.
        Context(std::string_view suiteName, const Types::ReportOptions &options);
        ~Context();

        Context(const Context &) = delete;
        Context &operator=(const Context &) = delete;

        /// @name Recording methods
        /// @{

        /// @brief Writes an informational line without changing result counts.
        /// @param message Text written after the `[INFO]` category and suite name.
        void info(std::string_view message);
        /// @brief Writes a manual-check line without changing result counts.
        /// @param instruction Human-facing manual check instruction.
        void manual(std::string_view instruction);
        /// @brief Writes a metric line without changing result counts.
        /// @param message Metric text written after the `[METRIC]` category.
        void metric(std::string_view message);
        /// @brief Writes a stress-test line without changing result counts.
        /// @param message Stress-test diagnostic text.
        void stress(std::string_view message);
        /// @brief Writes a summary line without changing result counts.
        /// @param message Summary text written after the `[SUMMARY]` category.
        void summary(std::string_view message);

        /// @brief Records one passing check or scenario.
        /// @param name Check or scenario name written into the report.
        void pass(std::string_view name);
        /// @brief Records one failing check or scenario with source-location details.
        /// @param name Check or scenario name written into the report.
        /// @param reason Failure reason written into the report.
        /// @param location Source location attached to the failure line.
        void fail(std::string_view name, std::string_view reason, std::source_location location = std::source_location::current());

        /// @brief Records one skipped check or scenario.
        /// @param name Check or scenario name written into the report.
        /// @param reason Skip reason written into the report.
        void skip(std::string_view name, std::string_view reason);
        /// @}

        /// @name Expectations
        /// @{

        /// @brief Expects value to be true.
        /// @param name Check name written into the report.
        /// @param value Boolean value to validate.
        /// @param location Source location attached when the expectation fails.
        /// @return True when value is true.
        [[nodiscard]] bool expectTrue(std::string_view name, bool value, std::source_location location = std::source_location::current());

        /// @brief Expects value to be false.
        /// @param name Check name written into the report.
        /// @param value Boolean value to validate.
        /// @param location Source location attached when the expectation fails.
        /// @return True when value is false.
        [[nodiscard]] bool expectFalse(std::string_view name, bool value, std::source_location location = std::source_location::current());

        /// @brief Expects expected and actual to compare equal.
        /// @tparam Expected Expected-value type.
        /// @tparam Actual Actual-value type.
        /// @param name Check name written into the report.
        /// @param expected Value expected by the test.
        /// @param actual Value produced by the code under test.
        /// @param location Source location attached when the expectation fails.
        /// @return True when `expected == actual`.
        template <typename Expected, typename Actual>
        [[nodiscard]] bool expectEq(
            std::string_view name,
            const Expected &expected,
            const Actual &actual,
            std::source_location location = std::source_location::current());

        /// @brief Expects unexpected and actual to compare not equal.
        /// @tparam Unexpected Disallowed-value type.
        /// @tparam Actual Actual-value type.
        /// @param name Check name written into the report.
        /// @param unexpected Value that actual must not equal.
        /// @param actual Value produced by the code under test.
        /// @param location Source location attached when the expectation fails.
        /// @return True when `unexpected != actual`.
        template <typename Unexpected, typename Actual>
        [[nodiscard]] bool expectNe(
            std::string_view name,
            const Unexpected &unexpected,
            const Actual &actual,
            std::source_location location = std::source_location::current());

        /// @brief Expects actual to be within tolerance of expected.
        /// @param name Check name written into the report.
        /// @param expected Expected numeric value.
        /// @param actual Actual numeric value.
        /// @param tolerance Allowed absolute difference. Negative tolerance is a failure.
        /// @param location Source location attached when the expectation fails.
        /// @return True when `abs(expected - actual) <= tolerance`.
        [[nodiscard]] bool expectNear(
            std::string_view name,
            double expected,
            double actual,
            double tolerance,
            std::source_location location = std::source_location::current());

        /// @brief Expects text to contain expectedSubstring.
        /// @param name Check name written into the report.
        /// @param text Text to search.
        /// @param expectedSubstring Substring that must appear in text. Empty substring passes.
        /// @param location Source location attached when the expectation fails.
        /// @return True when expectedSubstring appears in text.
        [[nodiscard]] bool expectContains(
            std::string_view name,
            std::string_view text,
            std::string_view expectedSubstring,
            std::source_location location = std::source_location::current());

        /// @brief Expects a text file to contain expectedSubstring.
        /// @param name Check name written into the report.
        /// @param path Text file to read.
        /// @param expectedSubstring Substring that must appear in the file.
        /// @param location Source location attached when the expectation fails.
        /// @return True when the file can be read and contains expectedSubstring.
        [[nodiscard]] bool expectFileContains(
            std::string_view name,
            const std::filesystem::path &path,
            std::string_view expectedSubstring,
            std::source_location location = std::source_location::current());

        /// @brief Expects a text file to contain text exactly expectedCount times.
        /// @param name Check name written into the report.
        /// @param path Text file to read.
        /// @param text Non-overlapping substring to count. Empty text counts as zero.
        /// @param expectedCount Required occurrence count.
        /// @param location Source location attached when the expectation fails.
        /// @return True when the observed occurrence count equals expectedCount.
        [[nodiscard]] bool expectFileOccurrenceCount(
            std::string_view name,
            const std::filesystem::path &path,
            std::string_view text,
            std::size_t expectedCount,
            std::source_location location = std::source_location::current());
        /// @}

        /// @name Result queries
        /// @{

        /// @brief Returns the suite name copied at construction.
        [[nodiscard]] const std::string &suiteName() const noexcept;
        /// @brief Returns a thread-safe snapshot of current counts.
        [[nodiscard]] Types::Summary result() const noexcept;
        /// @brief Returns result().ok().
        [[nodiscard]] bool ok() const noexcept;
        /// @}

    private:
        friend class Runner;

        Context(std::string_view suiteName, std::shared_ptr<Detail::ReportSink> reportSink);

        std::string suiteName_;
        std::shared_ptr<Detail::ReportSink> reportSink_;
        mutable std::mutex mutex_;
        Types::Summary summary_;

        void writeLine(std::string_view category, std::string_view message);
        void writeFailureLine(std::string_view name, std::string_view reason, const std::source_location &location);
    };

    /// @brief Runs named suites and aggregates one unified report.
    /// Thread-safety: runner recording methods serialize aggregate summary updates and report writes.
    class Runner
    {
    public:
        /// @brief Creates a runner with one shared report sink.
        explicit Runner(Types::ReportOptions options);
        ~Runner();

        Runner(const Runner &) = delete;
        Runner &operator=(const Runner &) = delete;

        /// @brief Runs a named suite and catches uncaught exceptions as failures.
        /// @tparam SuiteFunction Callable that accepts `Context&` or accepts no arguments.
        /// @param suiteName Display name written into report lines and the returned result.
        /// @param function Suite function to execute.
        /// @return Per-suite result, including counts and elapsed time.
        template <typename SuiteFunction> Types::SuiteResult runSuite(std::string_view suiteName, SuiteFunction &&function);

        /// @brief Writes a run-level informational line.
        /// @param message Text written after the run-level `[INFO]` category.
        void info(std::string_view message);
        /// @brief Writes a run-level summary line.
        /// @param message Text written after the run-level `[SUMMARY]` category.
        void summary(std::string_view message);

        /// @brief Returns aggregate counts across suites that have finished.
        [[nodiscard]] Types::Summary result() const noexcept;
        /// @brief Returns result().ok().
        [[nodiscard]] bool ok() const noexcept;
        /// @brief Returns zero when ok(), otherwise one.
        [[nodiscard]] int exitCode() const noexcept;

    private:
        std::shared_ptr<Detail::ReportSink> reportSink_;
        mutable std::mutex mutex_;
        Types::Summary summary_;

        void recordSuiteResult(const Types::SuiteResult &result);
    };

    /// @brief RAII helper that reports a named section and its elapsed time.
    class Section
    {
    public:
        /// @brief Begins a named section in context.
        /// @param context Context that receives section info and metric lines.
        /// @param name Section display name copied by the helper.
        Section(Context &context, std::string_view name);
        /// @brief Reports section duration.
        ~Section() noexcept;

        Section(const Section &) = delete;
        Section &operator=(const Section &) = delete;

    private:
        Context &context_;
        std::string name_;
        Timer timer_;
    };
    /// @}

    /// @name File helpers
    /// @{

    /// @brief Reads an entire text file, returning empty text when it cannot be opened.
    /// @param path Text file path.
    /// @return Full file contents, or empty text on open failure.
    [[nodiscard]] std::string readTextFile(const std::filesystem::path &path);

    /// @brief Writes a text file and creates parent directories as needed.
    /// @param path Text file path.
    /// @param text Contents written to the file.
    /// Failure behavior: throws std::runtime_error when the file cannot be opened or written.
    void writeTextFile(const std::filesystem::path &path, std::string_view text);

    /// @brief Returns true when path exists.
    /// @param path File or directory path.
    /// @return True when the filesystem reports that path exists.
    [[nodiscard]] bool fileExists(const std::filesystem::path &path) noexcept;

    /// @brief Returns true when a text file contains text.
    /// @param path Text file to read.
    /// @param text Substring to search for.
    /// @return True when text appears in the file contents.
    [[nodiscard]] bool fileContains(const std::filesystem::path &path, std::string_view text);

    /// @brief Counts non-overlapping occurrences of text in a text file.
    /// @param path Text file to read.
    /// @param text Substring to count. Empty text returns zero.
    /// @return Number of non-overlapping occurrences.
    [[nodiscard]] std::size_t countFileOccurrences(const std::filesystem::path &path, std::string_view text);

    /// @brief Creates a directory tree when path is non-empty.
    /// @param path Directory path to create.
    void createDirectories(const std::filesystem::path &path);

    /// @brief Removes a file or directory tree when it exists.
    /// @param path File or directory tree to remove.
    /// @note Removal errors are intentionally ignored for cleanup convenience in tests.
    void removeIfExists(const std::filesystem::path &path);
    /// @}

    /// @name Environment helpers
    /// @{

    /// @brief Temporarily sets an environment variable and restores the previous state on destruction.
    /// Thread-safety: process environment mutation is serialized inside this library. Other process environment access is still process-global.
    class ScopedEnvironmentVariable
    {
    public:
        /// @brief Sets name to value and stores the previous value, if any.
        /// @param name Environment variable name.
        /// @param value Temporary value visible to `std::getenv()` and child processes.
        ScopedEnvironmentVariable(std::string_view name, std::string_view value);
        /// @brief Restores the previous value or unsets name when it was previously missing.
        ~ScopedEnvironmentVariable();

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
        ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

        ScopedEnvironmentVariable(ScopedEnvironmentVariable &&) = delete;
        ScopedEnvironmentVariable &operator=(ScopedEnvironmentVariable &&) = delete;

    private:
        std::string name_;
        std::optional<std::string> previousValue_;
    };

    /// @brief Temporarily unsets an environment variable and restores the previous state on destruction.
    class ScopedUnsetEnvironmentVariable
    {
    public:
        /// @brief Unsets name and stores the previous value, if any.
        /// @param name Environment variable name to unset temporarily.
        explicit ScopedUnsetEnvironmentVariable(std::string_view name);
        /// @brief Restores the previous value or leaves name unset when it was previously missing.
        ~ScopedUnsetEnvironmentVariable();

        ScopedUnsetEnvironmentVariable(const ScopedUnsetEnvironmentVariable &) = delete;
        ScopedUnsetEnvironmentVariable &operator=(const ScopedUnsetEnvironmentVariable &) = delete;

        ScopedUnsetEnvironmentVariable(ScopedUnsetEnvironmentVariable &&) = delete;
        ScopedUnsetEnvironmentVariable &operator=(ScopedUnsetEnvironmentVariable &&) = delete;

    private:
        std::string name_;
        std::optional<std::string> previousValue_;
    };
    /// @}

    /// @name Child-process and manual-check types
    /// @{

    namespace Types
    {
        /// @brief Child-process environment override. A missing value unsets the variable for the child.
        struct EnvironmentVariable
        {
            /// @brief Environment variable name.
            std::string name;
            /// @brief Value to set. std::nullopt unsets the variable for the child.
            std::optional<std::string> value;
        };

        /// @brief Runtime options for one child-process execution.
        struct ChildProcessOptions
        {
            /// @brief Executable path to launch.
            std::filesystem::path executablePath;
            /// @brief Command-line arguments passed after executablePath.
            std::vector<std::string> arguments;

            /// @brief Environment overrides and unsets applied to the child.
            std::vector<EnvironmentVariable> environment;

            /// @brief Maximum time to wait before terminating the child.
            std::chrono::milliseconds timeout{5000};

            /// @brief Captures stdout and stderr into ChildProcessResult::output when true.
            bool captureOutput = true;
            /// @brief Starts from the parent environment before applying environment overrides when true.
            bool inheritParentEnvironment = true;
        };

        /// @brief Result of one child-process execution.
        struct ChildProcessResult
        {
            /// @brief Process exit code, or -1 when the process could not be launched or inspected.
            int exitCode = 0;
            /// @brief True when timeout expired before the child exited.
            bool timedOut = false;
            /// @brief True when the TestSupport library terminated the child after timeout.
            bool wasTerminatedByTest = false;

            /// @brief Captured stdout/stderr text when ChildProcessOptions::captureOutput is true.
            std::string output;

            /// @brief Returns true only for a zero exit without timeout or test termination.
            [[nodiscard]] bool exitedSuccessfully() const noexcept;
            /// @brief Returns true for nonzero exit, timeout, or test-requested termination.
            [[nodiscard]] bool exitedWithFailure() const noexcept;
        };

        /// @brief Answer selected by a human for a manual check.
        enum class ManualAnswer
        {
            /// @brief The user accepted or confirmed the manual check.
            Yes,
            /// @brief The user rejected or failed the manual check.
            No,
            /// @brief The user skipped the manual check or input ended before an answer.
            Skipped
        };
    } // namespace Types
    /// @}

    /// @name Child-process and manual-check helpers
    /// @{

    /// @brief Runs one child process.
    /// @param options Launch path, arguments, environment, timeout, and capture settings.
    /// @return Exit, timeout, termination, and optional output details.
    [[nodiscard]] Types::ChildProcessResult runChildProcess(const Types::ChildProcessOptions &options);

    /// @brief Prompts the user for a manual yes/no/skipped check.
    /// Blocking behavior: blocks on standard input when the process has an interactive input stream.
    /// @param question Prompt text shown to the user.
    /// @return Manual answer selected by the user, or Types::ManualAnswer::Skipped on EOF.
    Types::ManualAnswer promptManualCheck(std::string_view question);
    /// @}

    /// @name Stress helpers
    /// @{

    /// @brief Gate that blocks worker threads until opened.
    class StartGate
    {
    public:
        /// @brief Waits until open() is called.
        void wait();
        /// @brief Opens the gate and releases every waiter.
        void open();

    private:
        std::mutex mutex_;
        std::condition_variable condition_;
        bool open_ = false;
    };

    /// @brief Atomic stop flag for stress workers.
    class StopFlag
    {
    public:
        /// @brief Requests cooperative stop.
        void requestStop() noexcept;
        /// @brief Returns true after requestStop() has been called.
        [[nodiscard]] bool stopRequested() const noexcept;

    private:
        std::atomic<bool> stopRequested_{false};
    };

    /// @brief Starts workerCount threads, joins them, and rethrows the first worker exception.
    /// @tparam WorkerFunction Copy-constructible callable that accepts `std::size_t` worker index or accepts no arguments.
    /// @param workerCount Number of worker threads to start.
    /// @param workerFunction Prototype callable copied once into each worker thread.
    /// @note All started workers are joined before any captured exception is rethrown.
    /// @note Each worker receives its own callable copy, so mutable callable state is not shared between workers.
    template <typename WorkerFunction> void runWorkers(std::size_t workerCount, WorkerFunction &&workerFunction);
    /// @}

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

    template <typename SuiteFunction> Types::SuiteResult Runner::runSuite(std::string_view suiteName, SuiteFunction &&function)
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

        Types::SuiteResult suiteResult;
        suiteResult.name = std::string(suiteName);
        suiteResult.summary = context.result();
        suiteResult.elapsedMilliseconds = timer.elapsedMilliseconds();
        recordSuiteResult(suiteResult);
        return suiteResult;
    }

    template <typename WorkerFunction> void runWorkers(std::size_t workerCount, WorkerFunction &&workerFunction)
    {
        using Worker = std::decay_t<WorkerFunction>;
        static_assert(
            std::is_copy_constructible_v<Worker>,
            "WorkerFunction must be copy constructible so each worker can receive independent callable state.");

        if (workerCount == 0)
        {
            return;
        }

        Worker workerPrototype(std::forward<WorkerFunction>(workerFunction));
        std::mutex exceptionMutex;
        std::exception_ptr firstException;
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        try
        {
            for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
            {
                workers.emplace_back(
                    [workerIndex, worker = workerPrototype, &exceptionMutex, &firstException]() mutable
                    {
                        try
                        {
                            if constexpr (std::is_invocable_v<Worker &, std::size_t>)
                            {
                                worker(workerIndex);
                            }
                            else if constexpr (std::is_invocable_v<Worker &>)
                            {
                                worker();
                            }
                            else
                            {
                                static_assert(
                                    std::is_invocable_v<Worker &, std::size_t>,
                                    "WorkerFunction must be invocable with size_t or with no arguments.");
                            }
                        }
                        catch (...)
                        {
                            std::lock_guard lock(exceptionMutex);
                            if (!firstException)
                            {
                                firstException = std::current_exception();
                            }
                        }
                    });
            }
        }
        catch (...)
        {
            for (std::thread &worker : workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
            throw;
        }

        for (std::thread &worker : workers)
        {
            worker.join();
        }

        if (firstException)
        {
            std::rethrow_exception(firstException);
        }
    }
} // namespace GameWIP::TestSupport
