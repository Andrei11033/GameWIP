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
#include <cstdint>
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
        /// @brief Portable error categories for TestSupport-owned infrastructure operations.
        /// @note Enumerator numeric values are not serialization identifiers or stable wire-format values.
        enum class InfrastructureError : std::uint8_t
        {
            /// @brief The infrastructure operation completed successfully.
            None,
            /// @brief An argument was invalid after control entered the operation.
            InvalidArgument,
            /// @brief The requested operation has no supported backend on the current platform.
            Unsupported,
            /// @brief TestSupport could not allocate implementation-owned storage.
            OutOfMemory,
            /// @brief Native process state, handles, jobs, attributes, or startup sequencing could not be prepared.
            ProcessSetupFailed,
            /// @brief The child executable could not be launched.
            ProcessLaunchFailed,
            /// @brief Timeout or failure cleanup could not be completed as requested.
            ProcessCleanupFailed,
            /// @brief The child-output pipe could not be created or configured.
            PipeCreationFailed,
            /// @brief Output capture setup, reading, or worker execution failed.
            CaptureFailed,
            /// @brief Waiting for a child process failed.
            WaitFailed,
            /// @brief A started child process could not be inspected for its exit code.
            ProcessInspectionFailed,
            /// @brief An environment read or mutation failed.
            EnvironmentFailed,
            /// @brief A filesystem or text-file operation failed.
            FileOperationFailed,
            /// @brief A platform operation failed without a more specific stable category.
            PlatformFailure
        };

        /// @brief Compact status returned by expected TestSupport infrastructure operations.
        /// @details A successful status has `error == InfrastructureError::None` and `nativeCode == 0`.
        /// A failed status preserves a platform-native or standard-library diagnostic code when one is available;
        /// zero means that no numeric diagnostic was available.
        struct InfrastructureStatus
        {
            /// @brief Stable TestSupport-owned error category.
            InfrastructureError error = InfrastructureError::None;
            /// @brief Platform-native diagnostic code, or zero when unavailable.
            std::uint64_t nativeCode = 0;

            /// @brief Returns true only when the infrastructure operation completed successfully.
            [[nodiscard]] constexpr bool ok() const noexcept
            {
                return error == InfrastructureError::None;
            }
        };

        /// @brief Result returned by a text-producing infrastructure operation.
        struct TextResult
        {
            /// @brief Infrastructure operation status.
            InfrastructureStatus status;
            /// @brief Text retained by the operation; a failed result may contain useful partial text.
            std::string text;
        };

        /// @brief Result returned by a boolean infrastructure query.
        struct BoolResult
        {
            /// @brief Infrastructure operation status.
            InfrastructureStatus status;
            /// @brief Domain value. Interpret this field only as documented by the owning operation.
            bool value = false;
        };

        /// @brief Result returned by a counting infrastructure query.
        struct CountResult
        {
            /// @brief Infrastructure operation status.
            InfrastructureStatus status;
            /// @brief Count produced by the operation; zero remains a valid successful value.
            std::size_t count = 0;
        };

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

    /// @name Infrastructure status helpers
    /// @{

    /// @brief Formats a TestSupport infrastructure status for human-readable reporting.
    /// @param status Status to describe.
    /// @return Stable error-category name plus the numeric native diagnostic when present.
    /// @note Formatting is performed only when this function is called and may allocate.
    [[nodiscard]] std::string formatInfrastructureStatus(const Types::InfrastructureStatus &status);
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

        /// @brief Expects `fileContains(path, expectedSubstring)` to return successful status and true.
        /// @param name Check name written into the report.
        /// @param path Text file to read.
        /// @param expectedSubstring Substring that must appear in the successfully read file text.
        /// @param location Source location attached when the expectation fails.
        /// @return True when the file read succeeds and the returned value is true.
        [[nodiscard]] bool expectFileContains(
            std::string_view name,
            const std::filesystem::path &path,
            std::string_view expectedSubstring,
            std::source_location location = std::source_location::current());

        /// @brief Expects `countFileOccurrences(path, text)` to return successful status and expectedCount.
        /// @param name Check name written into the report.
        /// @param path Text file to read.
        /// @param text Non-overlapping substring to count. Empty text counts as zero.
        /// @param expectedCount Required occurrence count.
        /// @param location Source location attached when the expectation fails.
        /// @return True when the helper's observed count equals expectedCount.
        /// @note Failed reads are reported as expectation failures independently from a returned zero count.
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
    /// The non-copyable, non-movable guard removes its complete tree at destruction. Non-throwing construction
    /// uses a sanitized readable purpose plus bounded collision retries and leaves the guard inert on failure. Destruction is best effort;
    /// locked resources, external activity, or abnormal process termination can leave artifacts.
    class ScopedTemporaryDirectory
    {
    public:
        /// @brief Attempts to create a unique temporary directory using purpose as its readable name prefix.
        explicit ScopedTemporaryDirectory(std::string_view purpose = "test") noexcept;
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
        /// @brief Returns the construction status. A failed guard is inert and has an empty path.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::filesystem::path path_;
        std::filesystem::path root_;
        Types::InfrastructureStatus status_;
    };

    /// @brief Temporarily changes the process working directory and restores it on destruction.
    ///
    /// The current directory is process-global. Safe use requires strict LIFO scope ownership and
    /// no unrelated relative-path resolution or direct current-directory mutation. Failed construction
    /// leaves an inert guard; destruction performs a best-effort restore and suppresses errors.
    class ScopedCurrentPath
    {
    public:
        /// @brief Attempts to store the current working directory and change it to path.
        explicit ScopedCurrentPath(const std::filesystem::path &path) noexcept;
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
        /// @brief Returns the construction status. A failed guard performs no restoration.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::filesystem::path previousPath_;
        Types::InfrastructureStatus status_;
    };

    /// @brief Reads one file in binary mode through a single non-throwing whole-file convenience operation.
    /// @param path Text file path.
    /// @return Infrastructure status and bytes read. Empty text remains a successful empty-file result.
    /// @note The function performs no encoding, BOM, or newline conversion. A failed result may retain bytes read before failure.
    [[nodiscard]] Types::TextResult readTextFile(const std::filesystem::path &path) noexcept;

    /// @brief Creates parent directories and writes one file in binary truncate mode.
    /// @param path Text file path.
    /// @param text Bytes written unchanged; no encoding or newline conversion is performed.
    /// @return Success, or an explicit file/allocation failure with a native diagnostic when available.
    [[nodiscard]] Types::InfrastructureStatus writeTextFile(const std::filesystem::path &path, std::string_view text) noexcept;

    /// @brief Queries whether path exists without throwing filesystem errors.
    /// @param path File or directory path.
    /// @return Successful false for absence, successful true for existence, or failed status for an inspection error.
    [[nodiscard]] Types::BoolResult fileExists(const std::filesystem::path &path) noexcept;

    /// @brief Searches the bytes returned by `readTextFile()`.
    /// @param path Text file to read.
    /// @param text Substring to search for. An empty substring succeeds for any existing path whose read returns empty text.
    /// @return Read status plus whether text appears in the returned bytes.
    [[nodiscard]] Types::BoolResult fileContains(const std::filesystem::path &path, std::string_view text) noexcept;

    /// @brief Counts non-overlapping occurrences in the bytes returned by `readTextFile()`.
    /// @param path Text file to read.
    /// @param text Substring to count. Empty text returns zero.
    /// @return Read status plus the number of non-overlapping occurrences. Zero remains a valid successful count.
    [[nodiscard]] Types::CountResult countFileOccurrences(const std::filesystem::path &path, std::string_view text) noexcept;

    /// @brief Creates a directory tree when path is non-empty.
    /// @param path Directory path to create; an empty path is a no-op.
    /// @return Success for an empty path, an existing directory tree, or successful creation; otherwise a failed status.
    [[nodiscard]] Types::InfrastructureStatus createDirectories(const std::filesystem::path &path) noexcept;

    /// @brief Removes a file or complete directory tree without throwing expected infrastructure failures.
    /// @param path File or directory tree to remove.
    /// @return Success when path is absent or removed, otherwise a failed status.
    [[nodiscard]] Types::InfrastructureStatus removeIfExists(const std::filesystem::path &path) noexcept;
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
        /// @brief Attempts to set name to value and store the previous value, if any.
        /// @param name Environment variable name.
        /// @param value Temporary UTF-8 value visible to `std::getenv()` and child processes. An empty value removes the variable on Win32.
        ScopedEnvironmentVariable(std::string_view name, std::string_view value) noexcept;
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

        /// @brief Returns the construction status. A failed guard is inert.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::string name_;
        std::optional<std::string> previousValue_;
        Types::InfrastructureStatus status_;
    };

    /// @brief Temporarily unsets an environment variable and restores the previous state on destruction.
    class ScopedUnsetEnvironmentVariable
    {
    public:
        /// @brief Attempts to unset name and store the previous value, if any.
        /// @param name Environment variable name to unset temporarily.
        explicit ScopedUnsetEnvironmentVariable(std::string_view name) noexcept;
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

        /// @brief Returns the construction status. A failed guard is inert.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::string name_;
        std::optional<std::string> previousValue_;
        Types::InfrastructureStatus status_;
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

        /// @brief Observable outcome of one child process, separate from TestSupport infrastructure status.
        enum class ChildProcessOutcome : std::uint8_t
        {
            /// @brief The child process was never created.
            NotStarted,
            /// @brief The child exited and `ChildProcessResult::exitCode` is exact.
            Exited,
            /// @brief The configured wait expired; timeout is a successful domain outcome when enforcement succeeds.
            TimedOut,
            /// @brief TestSupport requested termination while recovering from an infrastructure failure.
            TerminatedDuringCleanup,
            /// @brief The child started, but its final outcome could not be inspected.
            OutcomeUnavailable
        };

        /// @brief Result of one child-process execution.
        struct ChildProcessResult
        {
            /// @brief TestSupport infrastructure status, independent of the child-process outcome.
            InfrastructureStatus status;
            /// @brief Complete native exit code, meaningful only when `outcome == ChildProcessOutcome::Exited`.
            std::uint32_t exitCode = 0;
            /// @brief Observable child-process outcome.
            ChildProcessOutcome outcome = ChildProcessOutcome::NotStarted;
            /// @brief True when capture stayed active but discarded bytes beyond the retained limit.
            bool outputTruncated = false;
            /// @brief Retained combined stdout/stderr bytes when capture is enabled; a failed result may preserve useful partial output.
            std::string output;
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
    /// @return Infrastructure status, child outcome, exact exit code when available, and optional raw output details.
    /// @note The child inherits the parent working directory. Timeout is not a strict total-duration deadline.
    /// @note Invalid process text, unsupported platforms, native failures, and implementation allocation failures are returned through status.
    /// Caller-side construction of allocating option fields remains governed by their standard-library types.
    [[nodiscard]] Types::ChildProcessResult runChildProcess(const Types::ChildProcessOptions &options) noexcept;

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
