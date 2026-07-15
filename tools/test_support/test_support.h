/// @file test_support.h
/// @brief Lightweight helpers for writing executable test suites.
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

/// @brief Test reporting, isolation, child-process, timing, and stress helpers.
namespace GameWIP::TestSupport
{
    /// @brief Default number of combined child stdout/stderr bytes retained in memory.
    /// @note Capture continues draining after this limit; excess bytes are discarded and reported through `outputTruncated`.
    inline constexpr std::size_t kDefaultMaxCapturedOutputBytes = std::size_t{4} * 1024 * 1024;

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

    /// @name Reporting and result types
    /// @{

    /// @brief Report, suite, child-process, and manual-check types.
    namespace Types
    {
        /// @brief Controls which report categories are mirrored to stdout.
        enum class ConsoleVerbosity
        {
            /// @brief Writes only failures, skips, and manual instructions.
            Minimal,
            /// @brief Writes failures, skips, manual instructions, suite results, and summaries.
            Concise,
            /// @brief Writes every report category, including passing checks and diagnostics.
            Full,
        };

        /// @brief Runtime report-output settings shared by test suites.
        /// Contract: these options control only test-run behavior. They do not enable or disable compiled code paths.
        struct ReportOptions
        {
            /// @brief Writes report lines to stdout when true.
            bool writeConsole = true;
            /// @brief Enables report-file setup when true and `reportPath` is non-empty.
            bool writeReport = true;
            /// @brief Appends instead of truncating when this sink opens `reportPath`.
            bool appendReport = false;
            /// @brief Flushes the report file after each line; this does not explicitly flush stdout.
            bool flushReportEachLine = false;
            /// @brief Controls which report categories are written to stdout.
            ConsoleVerbosity consoleVerbosity = ConsoleVerbosity::Full;

            /// @brief Text report path used when file output is enabled. Empty path opens no file.
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
            /// @brief Returns true when failed is zero, including empty and skipped-only summaries.
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

    } // namespace Types
    /// @}

    /// @name Timing helpers
    /// @{

    /// @brief Monotonic elapsed-time helper for test diagnostics.
    /// @note Timer uses `std::chrono::steady_clock` and is not benchmark-grade measurement.
    /// @note Concurrent reset/read of the same Timer is not internally synchronized.
    class Timer
    {
    public:
        /// @brief Starts the timer at construction.
        Timer() noexcept;

        /// @brief Restarts elapsed-time measurement from now.
        void reset() noexcept;

        /// @brief Returns elapsed wall-clock time in milliseconds.
        [[nodiscard]] double elapsedMilliseconds() const noexcept;

    private:
        using Clock = std::chrono::steady_clock;

        Clock::time_point start_;
    };
    /// @}

    /// @name Suite reporting
    /// @{

    /// @brief Test context passed to suite functions.
    ///
    /// Public recording methods serialize summary updates and report-sink access. Expectations
    /// record exactly one pass or failure and return the same outcome instead of aborting.
    /// Ordinary report-file stream failures disable only that sink and do not change result counts.
    /// Formatting, allocation, path-conversion, and standard-stream exceptions may still propagate
    /// from methods that are not marked `noexcept`.
    class Context
    {
    public:
        /// @brief Creates a standalone context with its own report sink.
        /// @param suiteName Name written into report lines.
        /// @param options Runtime output options.
        Context(std::string_view suiteName, const Types::ReportOptions &options);
        /// @brief Releases this context and its shared report-sink ownership.
        ~Context();

        /// @brief Context owns synchronization and a report-sink reference and is not copyable.
        Context(const Context &) = delete;
        /// @brief Context owns synchronization and a report-sink reference and is not copy-assignable.
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

        /// @brief Expects `fileContains(path, expectedSubstring)` to succeed.
        /// @param name Check name written into the report.
        /// @param path Text file to read.
        /// @param expectedSubstring Substring that must appear in the helper's returned text. An empty substring inherits `fileContains()` ambiguity.
        /// @param location Source location attached when the expectation fails.
        /// @return True when `fileContains()` returns true.
        /// @note Use `fileExists()` or a detailed I/O API when existence/readability is a separate requirement.
        [[nodiscard]] bool expectFileContains(
            std::string_view name,
            const std::filesystem::path &path,
            std::string_view expectedSubstring,
            std::source_location location = std::source_location::current());

        /// @brief Expects `countFileOccurrences(path, text)` to equal expectedCount.
        /// @param name Check name written into the report.
        /// @param path Text file to read.
        /// @param text Non-overlapping substring to count. Empty text counts as zero.
        /// @param expectedCount Required occurrence count.
        /// @param location Source location attached when the expectation fails.
        /// @return True when the helper's observed count equals expectedCount.
        /// @note A zero count does not distinguish no matches from missing or unreadable input.
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
        /// @return Object-owned reference valid until this Context is destroyed.
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

    /// @brief Runs named suites and aggregates one shared report.
    ///
    /// Aggregate updates and report-sink access are serialized. Concurrent suite completion order
    /// is not deterministic; `result()` contains only suites that have completed.
    class Runner
    {
    public:
        /// @brief Creates a runner with one shared report sink.
        explicit Runner(Types::ReportOptions options);
        /// @brief Releases the runner's ownership of its shared report sink.
        ~Runner();

        /// @brief Runner owns synchronization and one shared report sink and is not copyable.
        Runner(const Runner &) = delete;
        /// @brief Runner owns synchronization and one shared report sink and is not copy-assignable.
        Runner &operator=(const Runner &) = delete;

        /// @brief Runs a named suite and converts an uncaught suite exception into one failed check.
        /// @tparam SuiteFunction Callable that accepts `Context&` or accepts no arguments. The `Context&` form is selected when both are viable.
        /// @param suiteName Display name copied into report lines and the returned result.
        /// @param function Suite function to execute.
        /// @return Per-suite result, including counts and elapsed time, after it has been added to the aggregate.
        /// @note Exceptions thrown while recording the converted failure or final result can still propagate.
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
        /// @brief Returns zero when no failures have been recorded, otherwise one.
        /// @note An empty or skipped-only run returns zero; report-file health is not reflected.
        [[nodiscard]] int exitCode() const noexcept;

    private:
        std::shared_ptr<Detail::ReportSink> reportSink_;
        mutable std::mutex mutex_;
        Types::Summary summary_;

        void recordSuiteResult(const Types::SuiteResult &result);
    };

    /// @brief RAII helper that reports a named section and its elapsed time.
    /// @note The referenced Context must outlive the Section. Section reporting does not affect result counts.
    class Section
    {
    public:
        /// @brief Begins a named section in context.
        /// @param context Context that receives section info and metric lines.
        /// @param name Section display name copied by the helper.
        Section(Context &context, std::string_view name);
        /// @brief Attempts to report section duration.
        /// @note Formatting or reporting failures are suppressed because the destructor cannot throw.
        ~Section() noexcept;

        /// @brief Section owns a context reference and is not copyable.
        Section(const Section &) = delete;
        /// @brief Section owns a context reference and is not copy-assignable.
        Section &operator=(const Section &) = delete;

    private:
        Context &context_;
        std::string name_;
        Timer timer_;
    };
    /// @}

    /// @name File helpers
    /// @{

    /// @brief Owns one unique directory under the operating-system temporary directory.
    ///
    /// The non-copyable, non-movable guard removes its complete tree at destruction. Construction
    /// uses a sanitized readable purpose plus bounded collision retries. Destruction is best effort;
    /// locked resources, external activity, or abnormal process termination can leave artifacts.
    class ScopedTemporaryDirectory
    {
    public:
        /// @brief Creates a unique temporary directory using purpose as its readable name prefix.
        /// @throws std::filesystem::filesystem_error when the temporary root cannot be resolved or created.
        explicit ScopedTemporaryDirectory(std::string_view purpose = "test");
        /// @brief Best-effort removes the owned directory tree and empty GameWIP temp parents.
        ~ScopedTemporaryDirectory() noexcept;

        /// @brief Temporary-directory ownership cannot be copied.
        ScopedTemporaryDirectory(const ScopedTemporaryDirectory &) = delete;
        /// @brief Temporary-directory ownership cannot be copy-assigned.
        ScopedTemporaryDirectory &operator=(const ScopedTemporaryDirectory &) = delete;
        /// @brief Temporary-directory ownership cannot be moved.
        ScopedTemporaryDirectory(ScopedTemporaryDirectory &&) = delete;
        /// @brief Temporary-directory ownership cannot be move-assigned.
        ScopedTemporaryDirectory &operator=(ScopedTemporaryDirectory &&) = delete;

        /// @brief Returns the owned temporary directory path.
        /// @return Object-owned reference valid until this guard is destroyed.
        [[nodiscard]] const std::filesystem::path &path() const noexcept;

    private:
        std::filesystem::path path_;
        std::filesystem::path root_;
    };

    /// @brief Temporarily changes the process working directory and restores it on destruction.
    ///
    /// The current directory is process-global. Safe use requires strict LIFO scope ownership and
    /// no unrelated relative-path resolution or direct current-directory mutation. Construction
    /// throws on setup failure; destruction performs a best-effort restore and suppresses errors.
    class ScopedCurrentPath
    {
    public:
        /// @brief Stores the current working directory and changes it to path.
        /// @throws std::filesystem::filesystem_error when the current directory cannot be read or changed.
        explicit ScopedCurrentPath(const std::filesystem::path &path);
        /// @brief Best-effort restores the captured working directory.
        ~ScopedCurrentPath() noexcept;

        /// @brief Process-global current-directory ownership cannot be copied.
        ScopedCurrentPath(const ScopedCurrentPath &) = delete;
        /// @brief Process-global current-directory ownership cannot be copy-assigned.
        ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;
        /// @brief Process-global current-directory ownership cannot be moved.
        ScopedCurrentPath(ScopedCurrentPath &&) = delete;
        /// @brief Process-global current-directory ownership cannot be move-assigned.
        ScopedCurrentPath &operator=(ScopedCurrentPath &&) = delete;

        /// @brief Returns the working directory that will be restored.
        /// @return Object-owned reference valid until this guard is destroyed.
        [[nodiscard]] const std::filesystem::path &previousPath() const noexcept;

    private:
        std::filesystem::path previousPath_;
    };

    /// @brief Reads one file in binary mode through a single whole-file convenience operation.
    /// @param path Text file path.
    /// @return Read bytes, or empty text for an empty file, open failure, or non-positive initial size.
    /// @note The function performs no encoding, BOM, or newline conversion and cannot distinguish its empty-result cases.
    /// @note A short stream read returns the bytes actually read; standard path, allocation, length, or stream exceptions may propagate.
    [[nodiscard]] std::string readTextFile(const std::filesystem::path &path);

    /// @brief Creates parent directories and writes one file in binary truncate mode.
    /// @param path Text file path.
    /// @param text Bytes written unchanged; no encoding or newline conversion is performed.
    /// @throws std::filesystem::filesystem_error when parent-directory creation fails.
    /// @throws std::runtime_error when the file cannot be opened or written.
    /// @throws std::exception Standard path, allocation, length, or stream exceptions may also propagate.
    void writeTextFile(const std::filesystem::path &path, std::string_view text);

    /// @brief Queries whether path exists without throwing filesystem errors.
    /// @param path File or directory path.
    /// @return True when the filesystem reports that path exists; false for missing paths and query errors.
    [[nodiscard]] bool fileExists(const std::filesystem::path &path) noexcept;

    /// @brief Searches the bytes returned by `readTextFile()` after an existence query.
    /// @param path Text file to read.
    /// @param text Substring to search for. An empty substring succeeds for any existing path whose read returns empty text.
    /// @return True when path exists and text appears in the helper's returned bytes.
    /// @note Open/read failure is not distinguishable from empty content.
    [[nodiscard]] bool fileContains(const std::filesystem::path &path, std::string_view text);

    /// @brief Counts non-overlapping occurrences in the bytes returned by `readTextFile()`.
    /// @param path Text file to read.
    /// @param text Substring to count. Empty text returns zero.
    /// @return Number of non-overlapping occurrences.
    /// @note Zero also represents missing/unreadable input and no matches.
    [[nodiscard]] std::size_t countFileOccurrences(const std::filesystem::path &path, std::string_view text);

    /// @brief Creates a directory tree when path is non-empty.
    /// @param path Directory path to create; an empty path is a no-op.
    /// @throws std::filesystem::filesystem_error when creation fails.
    void createDirectories(const std::filesystem::path &path);

    /// @brief Best-effort removes a file or complete directory tree.
    /// @param path File or directory tree to remove.
    /// @note All removal errors are intentionally suppressed for cleanup convenience.
    void removeIfExists(const std::filesystem::path &path);
    /// @}

    /// @name Environment helpers
    /// @{

    /// @brief Temporarily sets an environment variable and restores the previous state on destruction.
    ///
    /// TestSupport serializes each mutation/restoration operation, not the guard lifetime. The
    /// process environment remains global, and other environment APIs do not participate in that
    /// serialization. On Win32, an empty value follows `_wputenv_s` removal semantics.
    class ScopedEnvironmentVariable
    {
    public:
        /// @brief Sets name to value and stores the previous value, if any.
        /// @param name Environment variable name.
        /// @param value Temporary UTF-8 value visible to `std::getenv()` and child processes. An empty value removes the variable on Win32.
        /// @throws std::invalid_argument for an empty/invalid name, embedded null, or invalid UTF-8 text.
        /// @throws std::length_error when conversion input exceeds the Win32 limit.
        /// @throws std::runtime_error when conversion or environment mutation fails.
        ScopedEnvironmentVariable(std::string_view name, std::string_view value);
        /// @brief Best-effort restores the previous value or unsets name when it was previously missing.
        ~ScopedEnvironmentVariable() noexcept;

        /// @brief Process-global environment ownership cannot be copied.
        ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
        /// @brief Process-global environment ownership cannot be copy-assigned.
        ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

        /// @brief Process-global environment ownership cannot be moved.
        ScopedEnvironmentVariable(ScopedEnvironmentVariable &&) = delete;
        /// @brief Process-global environment ownership cannot be move-assigned.
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
        /// @throws std::invalid_argument for an empty/invalid name, embedded null, or invalid UTF-8 text.
        /// @throws std::length_error when conversion input exceeds the Win32 limit.
        /// @throws std::runtime_error when conversion or environment mutation fails.
        explicit ScopedUnsetEnvironmentVariable(std::string_view name);
        /// @brief Best-effort restores the previous value or leaves name unset when it was previously missing.
        ~ScopedUnsetEnvironmentVariable() noexcept;

        /// @brief Process-global environment ownership cannot be copied.
        ScopedUnsetEnvironmentVariable(const ScopedUnsetEnvironmentVariable &) = delete;
        /// @brief Process-global environment ownership cannot be copy-assigned.
        ScopedUnsetEnvironmentVariable &operator=(const ScopedUnsetEnvironmentVariable &) = delete;

        /// @brief Process-global environment ownership cannot be moved.
        ScopedUnsetEnvironmentVariable(ScopedUnsetEnvironmentVariable &&) = delete;
        /// @brief Process-global environment ownership cannot be move-assigned.
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

            /// @brief Environment overrides/unsets applied in vector order; later duplicate names win.
            std::vector<EnvironmentVariable> environment;

            /// @brief Wait before TestSupport begins termination. Negative waits indefinitely; zero polls immediately.
            /// @note Cleanup and output-reader shutdown can extend total call duration beyond this value.
            std::chrono::milliseconds timeout{5000};

            /// @brief Routes stdout and stderr to one combined capture pipe when true.
            bool captureOutput = true;
            /// @brief Retained capture limit; excess bytes are drained and discarded. Zero retains nothing.
            std::size_t maxCapturedOutputBytes = kDefaultMaxCapturedOutputBytes;
            /// @brief Copies the parent environment before overrides; false starts from an otherwise empty block.
            bool inheritParentEnvironment = true;
        };

        /// @brief Result of one child-process execution.
        struct ChildProcessResult
        {
            /// @brief Native process exit code narrowed to int, or -1 for launch/setup/wait/inspection/capture infrastructure failure.
            /// @warning On Win32, native codes above INT_MAX are not represented faithfully and 0xffffffff collides
            /// with the -1 infrastructure sentinel.
            int exitCode = 0;
            /// @brief True when the configured wait expired before normal process completion.
            bool timedOut = false;
            /// @brief True when TestSupport requested primary-process termination during timeout or failure handling.
            bool wasTerminatedByTest = false;

            /// @brief Retained combined stdout/stderr bytes when capture is enabled; no UTF-8 validation is performed.
            std::string output;
            /// @brief True when capture stayed active but discarded bytes beyond the retained limit.
            bool outputTruncated = false;

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

    /// @brief Launches one process directly without a shell and waits for its process tree to finish or be terminated.
    /// @param options Launch path, arguments, environment, timeout, and combined-output capture settings.
    /// @return Exit, timeout, termination, and optional raw output details. `exitCode == -1` reports TestSupport infrastructure failure.
    /// @note The child inherits the parent working directory. Timeout is not a strict total-duration deadline.
    /// @warning Win32 exit codes above INT_MAX are narrowed to int; 0xffffffff is indistinguishable from the -1 infrastructure sentinel.
    /// @throws std::invalid_argument when a path, argument, environment name, or environment value contains invalid process text.
    /// @throws std::length_error when UTF-8 input exceeds the platform conversion limit.
    /// @throws std::runtime_error when platform text conversion fails unexpectedly.
    [[nodiscard]] Types::ChildProcessResult runChildProcess(const Types::ChildProcessOptions &options);

    /// @brief Repeatedly prompts for a recognized yes/no/skip line.
    /// @param question Prompt text shown to the user.
    /// @return Selected answer, or `Skipped` on EOF.
    /// @note The function can block indefinitely, does not trim whitespace, and does not repair failed stream state.
    /// @throws std::ios_base::failure when standard streams are configured to throw.
    Types::ManualAnswer promptManualCheck(std::string_view question);
    /// @}

    /// @name Stress helpers
    /// @{

    /// @brief One-shot gate that blocks workers until opened.
    /// @note `open()` is idempotent; current and future waiters pass after opening and the gate cannot be reset.
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

    /// @brief One-way atomic cooperative-stop flag.
    /// @note `requestStop()` uses release ordering, `stopRequested()` uses acquire ordering, and there is no reset operation.
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

    /// @brief Starts workerCount threads, joins every started thread, and rethrows one captured failure.
    /// @tparam WorkerFunction Copy-constructible callable that accepts `std::size_t` or no arguments.
    /// The indexed form is selected when both are viable.
    /// @param workerCount Number of workers; zero starts no threads.
    /// @param workerFunction Prototype callable copied into each worker.
    /// @note Callable objects are separate, but captured references, pointers, and shared objects remain shared.
    /// @note Worker exceptions do not stop peers; one scheduling-dependent exception is rethrown after all joins.
    /// @note Startup allocation/thread/copy failure joins already-started workers before rethrowing.
    /// Coordination must tolerate a partially started set.
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
