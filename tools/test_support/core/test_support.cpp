/// @file test_support.cpp
/// @brief Core implementation for the TestSupport library.

#include "test_support/test_support.h"
#include "test_support/internal/test_support_platform.h"
#include "test_support/internal/test_support_test_hooks.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace GameWIP::TestSupport
{
    namespace
    {
        /// @brief Serializes individual environment read/mutate/restore operations made by TestSupport guards.
        /// @note A guard does not retain this mutex for its lifetime; overlapping scopes still require caller coordination.
        std::mutex environmentMutex;
        /// @brief Adds process-local uniqueness when time and readable purpose components collide.
        std::atomic_uint64_t temporaryDirectoryCounter{0};

        /// @brief Converts a standard error code to the public unsigned diagnostic field.
        [[nodiscard]] std::uint64_t nativeCode(const std::error_code &error) noexcept
        {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.value()));
        }

        /// @brief Creates a failed TestSupport infrastructure status without allocating.
        [[nodiscard]] Types::InfrastructureStatus failureStatus(Types::InfrastructureError error, std::uint64_t code = 0) noexcept
        {
            return Types::InfrastructureStatus{.error = error, .nativeCode = code};
        }

        /// @brief Formats a named failure and its reason for report output.
        [[nodiscard]] std::string makeNameReason(std::string_view name, std::string_view reason)
        {
            std::ostringstream message;
            message << name << ": " << reason;
            return message.str();
        }

        /// @brief Converts a human purpose into a portable filename component.
        [[nodiscard]] std::string sanitizeTemporaryPurpose(std::string_view purpose)
        {
            std::string sanitized;
            sanitized.reserve(purpose.size());
            for (const char character : purpose)
            {
                const bool asciiLetter = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
                const bool asciiDigit = character >= '0' && character <= '9';
                sanitized.push_back(asciiLetter || asciiDigit || character == '-' || character == '_' ? character : '_');
            }
            return sanitized.empty() ? "test" : sanitized;
        }

        /// @brief Best-effort removes a parent directory after its owned child was cleaned.
        void removeDirectoryIfEmpty(const std::filesystem::path &path) noexcept
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove(path, error));
        }
    } // namespace

    std::string formatInfrastructureStatus(const Types::InfrastructureStatus &status)
    {
        std::string_view errorName = "PlatformFailure";
        switch (status.error)
        {
        case Types::InfrastructureError::None:
            errorName = "None";
            break;
        case Types::InfrastructureError::InvalidArgument:
            errorName = "InvalidArgument";
            break;
        case Types::InfrastructureError::Unsupported:
            errorName = "Unsupported";
            break;
        case Types::InfrastructureError::OutOfMemory:
            errorName = "OutOfMemory";
            break;
        case Types::InfrastructureError::ProcessSetupFailed:
            errorName = "ProcessSetupFailed";
            break;
        case Types::InfrastructureError::ProcessLaunchFailed:
            errorName = "ProcessLaunchFailed";
            break;
        case Types::InfrastructureError::ProcessCleanupFailed:
            errorName = "ProcessCleanupFailed";
            break;
        case Types::InfrastructureError::PipeCreationFailed:
            errorName = "PipeCreationFailed";
            break;
        case Types::InfrastructureError::CaptureFailed:
            errorName = "CaptureFailed";
            break;
        case Types::InfrastructureError::WaitFailed:
            errorName = "WaitFailed";
            break;
        case Types::InfrastructureError::ProcessInspectionFailed:
            errorName = "ProcessInspectionFailed";
            break;
        case Types::InfrastructureError::EnvironmentFailed:
            errorName = "EnvironmentFailed";
            break;
        case Types::InfrastructureError::FileOperationFailed:
            errorName = "FileOperationFailed";
            break;
        case Types::InfrastructureError::PlatformFailure:
            break;
        }

        return status.nativeCode == 0 ? std::string(errorName) : std::format("{} (nativeCode={})", errorName, status.nativeCode);
    }

    namespace Detail
    {
        /// @brief Shared line sink with independent console policy and degradable report-file output.
        ///
        /// Sink locking protects complete line emission. Context and Runner counters deliberately use
        /// their own locks so counting does not hold the output lock while formatting or aggregation occurs.
        class ReportSink
        {
        public:
            /// @brief Opens configured report output and degrades to console-only on setup failure.
            explicit ReportSink(Types::ReportOptions options)
                : options_(std::move(options))
            {
                if (options_.writeReport && !options_.reportPath.empty())
                {
                    const std::filesystem::path parentPath = options_.reportPath.parent_path();
                    if (!parentPath.empty())
                    {
                        std::error_code error;
                        std::filesystem::create_directories(parentPath, error);
                        if (error)
                        {
                            disableReport(std::string("could not create the report directory: ") + error.message());
                            return;
                        }
                    }

                    const std::ios::openmode mode = options_.appendReport ? (std::ios::out | std::ios::app) : (std::ios::out | std::ios::trunc);
                    report_.open(options_.reportPath, mode);
                    if (!report_.is_open())
                    {
                        disableReport("could not open the report file");
                    }
                }
            }

            ~ReportSink()
            {
                flush();
            }

            /// @brief Writes a run-level report line without a suite label.
            void write(std::string_view category, std::string_view message)
            {
                write(category, {}, message);
            }

            /// @brief Formats and atomically routes one categorized report line.
            void write(std::string_view category, std::string_view suiteName, std::string_view message)
            {
                std::ostringstream line;
                line << '[' << category << ']';
                if (!suiteName.empty())
                {
                    line << " [" << suiteName << ']';
                }
                if (!message.empty())
                {
                    line << ' ' << message;
                }

                const std::string text = line.str();
                std::lock_guard lock(mutex_);

                if (shouldWriteToConsole(category))
                {
                    std::cout << text << '\n';
                }

                if (options_.writeReport && !options_.reportPath.empty() && !reportOpenFailed_)
                {
                    if (report_.is_open())
                    {
                        report_ << text << '\n';
                        if (options_.flushReportEachLine)
                        {
                            report_.flush();
                        }
                    }

                    if (!report_ || !report_.is_open())
                    {
                        disableReport("report output failed while writing");
                    }
                }
            }

            /// @brief Flushes retained file output without changing test results on failure.
            void flush()
            {
                std::lock_guard lock(mutex_);
                if (!reportOpenFailed_ && report_.is_open())
                {
                    report_.flush();
                    if (!report_)
                    {
                        disableReport("report output failed while flushing");
                    }
                }
            }

        private:
            /// @brief Applies console enablement and category verbosity policy.
            [[nodiscard]] bool shouldWriteToConsole(std::string_view category) const noexcept
            {
                if (!options_.writeConsole)
                {
                    return false;
                }
                if (options_.consoleVerbosity == Types::ConsoleVerbosity::Full)
                {
                    return true;
                }

                const bool isActionable = category == "FAIL" || category == "SKIP" || category == "MANUAL";
                if (options_.consoleVerbosity == Types::ConsoleVerbosity::Minimal)
                {
                    return isActionable;
                }

                return isActionable || category == "SUMMARY" || category == "RESULT";
            }

            /// @brief Permanently disables only this sink's file output and emits at most one stderr diagnostic.
            /// @note Test result counters remain independent from report-file health.
            void disableReport(std::string_view reason)
            {
                reportOpenFailed_ = true;
                report_.close();
                if (!reportFailureReported_)
                {
                    std::cerr << "[TEST REPORT] " << reason << ": " << options_.reportPath.string() << '\n';
                    reportFailureReported_ = true;
                }
            }

            Types::ReportOptions options_;
            std::ofstream report_;
            std::mutex mutex_;
            bool reportOpenFailed_ = false;
            bool reportFailureReported_ = false;
        };
    } // namespace Detail

    std::size_t Types::Summary::total() const noexcept
    {
        return passed + failed + skipped;
    }

    bool Types::Summary::ok() const noexcept
    {
        return failed == 0;
    }

    bool Types::SuiteResult::ok() const noexcept
    {
        return summary.ok();
    }

    Context::Context(std::string_view suiteName, const Types::ReportOptions &options)
        : Context(suiteName, std::make_shared<Detail::ReportSink>(options))
    {
    }

    Context::Context(std::string_view suiteName, std::shared_ptr<Detail::ReportSink> reportSink)
        : suiteName_(suiteName)
        , reportSink_(std::move(reportSink))
    {
    }

    Context::~Context() = default;

    void Context::info(std::string_view message)
    {
        writeLine("INFO", message);
    }

    void Context::manual(std::string_view instruction)
    {
        writeLine("MANUAL", instruction);
    }

    void Context::metric(std::string_view message)
    {
        writeLine("METRIC", message);
    }

    void Context::stress(std::string_view message)
    {
        writeLine("STRESS", message);
    }

    void Context::summary(std::string_view message)
    {
        writeLine("SUMMARY", message);
    }

    void Context::pass(std::string_view name)
    {
        {
            std::lock_guard lock(mutex_);
            ++summary_.passed;
        }

        writeLine("PASS", name);
    }

    void Context::fail(std::string_view name, std::string_view reason, std::source_location location)
    {
        {
            std::lock_guard lock(mutex_);
            ++summary_.failed;
        }

        writeFailureLine(name, reason, location);
    }

    void Context::skip(std::string_view name, std::string_view reason)
    {
        {
            std::lock_guard lock(mutex_);
            ++summary_.skipped;
        }

        writeLine("SKIP", makeNameReason(name, reason));
    }

    bool Context::expectTrue(std::string_view name, bool value, std::source_location location)
    {
        if (value)
        {
            pass(name);
            return true;
        }

        fail(name, "expected true", location);
        return false;
    }

    bool Context::expectFalse(std::string_view name, bool value, std::source_location location)
    {
        if (!value)
        {
            pass(name);
            return true;
        }

        fail(name, "expected false", location);
        return false;
    }

    bool Context::expectNear(std::string_view name, double expected, double actual, double tolerance, std::source_location location)
    {
        if (tolerance < 0.0)
        {
            fail(name, "tolerance must be non-negative", location);
            return false;
        }

        if (std::fabs(expected - actual) <= tolerance)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected " << expected << " +/- " << tolerance << ", got " << actual;
        fail(name, reason.str(), location);
        return false;
    }

    bool Context::expectContains(std::string_view name, std::string_view text, std::string_view expectedSubstring, std::source_location location)
    {
        if (text.find(expectedSubstring) != std::string_view::npos)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected text to contain '" << expectedSubstring << "'";
        fail(name, reason.str(), location);
        return false;
    }

    bool Context::expectFileContains(
        std::string_view name,
        const std::filesystem::path &path,
        std::string_view expectedSubstring,
        std::source_location location)
    {
        const Types::BoolResult result = fileContains(path, expectedSubstring);
        if (result.status.ok() && result.value)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected file '" << path.string() << "' to contain '" << expectedSubstring << "'";
        if (!result.status.ok())
        {
            reason << ", infrastructure error " << formatInfrastructureStatus(result.status);
        }
        fail(name, reason.str(), location);
        return false;
    }

    bool Context::expectFileOccurrenceCount(
        std::string_view name,
        const std::filesystem::path &path,
        std::string_view text,
        std::size_t expectedCount,
        std::source_location location)
    {
        const Types::CountResult result = countFileOccurrences(path, text);
        if (result.status.ok() && result.count == expectedCount)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected " << expectedCount << " occurrences of '" << text << "' in file '" << path.string() << "', got " << result.count;
        if (!result.status.ok())
        {
            reason << ", infrastructure error " << formatInfrastructureStatus(result.status);
        }
        fail(name, reason.str(), location);
        return false;
    }

    const std::string &Context::suiteName() const noexcept
    {
        return suiteName_;
    }

    Types::Summary Context::result() const noexcept
    {
        std::lock_guard lock(mutex_);
        return summary_;
    }

    bool Context::ok() const noexcept
    {
        return result().ok();
    }

    void Context::writeLine(std::string_view category, std::string_view message)
    {
        reportSink_->write(category, suiteName_, message);
    }

    void Context::writeFailureLine(std::string_view name, std::string_view reason, const std::source_location &location)
    {
        std::ostringstream message;
        message << name << ": " << reason << " (" << location.file_name() << ':' << location.line() << " in " << location.function_name() << ')';
        writeLine("FAIL", message.str());
    }

    Runner::Runner(Types::ReportOptions options)
        : reportSink_(std::make_shared<Detail::ReportSink>(std::move(options)))
    {
    }

    Runner::~Runner() = default;

    void Runner::info(std::string_view message)
    {
        reportSink_->write("INFO", message);
    }

    void Runner::summary(std::string_view message)
    {
        reportSink_->write("SUMMARY", message);
    }

    Types::Summary Runner::result() const noexcept
    {
        std::lock_guard lock(mutex_);
        return summary_;
    }

    bool Runner::ok() const noexcept
    {
        return result().ok();
    }

    int Runner::exitCode() const noexcept
    {
        return ok() ? 0 : 1;
    }

    void Runner::recordSuiteResult(const Types::SuiteResult &result)
    {
        {
            std::lock_guard lock(mutex_);
            summary_.passed += result.summary.passed;
            summary_.failed += result.summary.failed;
            summary_.skipped += result.summary.skipped;
        }

        // Aggregate counts are committed before formatting/output so result queries never depend on
        // report-file availability. The sink then serializes the visible completion record.
        std::ostringstream message;
        message << result.name << " passed=" << result.summary.passed << " failed=" << result.summary.failed << " skipped=" << result.summary.skipped
                << " elapsedMs=" << result.elapsedMilliseconds;
        reportSink_->write("RESULT", message.str());
        reportSink_->flush();
    }

    Section::Section(Context &context, std::string_view name)
        : context_(context)
        , name_(name)
    {
        std::ostringstream message;
        message << "begin section: " << name_;
        context_.info(message.str());
    }

    Section::~Section() noexcept
    {
        try
        {
            const double elapsedMilliseconds = timer_.elapsedMilliseconds();
            timer_.reset();

            std::ostringstream message;
            message << "section " << name_ << " elapsedMs=" << elapsedMilliseconds;
            context_.metric(message.str());
        }
        catch (...) // NOLINT(bugprone-empty-catch) -- A diagnostic-only destructor cannot propagate failures.
        {
        }
    }

    Timer::Timer() noexcept
        : start_(Clock::now())
    {
    }

    void Timer::reset() noexcept
    {
        start_ = Clock::now();
    }

    double Timer::elapsedMilliseconds() const noexcept
    {
        const auto elapsed = Clock::now() - start_;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }

    ScopedTemporaryDirectory::ScopedTemporaryDirectory(std::string_view purpose) noexcept
    {
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::TemporaryDirectory))
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return;
        }
#endif
        try
        {
            std::error_code error;
            root_ = std::filesystem::temp_directory_path(error);
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                return;
            }
            root_ /= "GameWIP";
            root_ /= "TestSupport";
            static_cast<void>(std::filesystem::create_directories(root_, error));
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                return;
            }

            // A readable prefix aids diagnostics, while steady-clock ticks and a process-local allocation
            // id make collisions unlikely. The bounded attempt suffix still handles races/external entries.
            const std::string prefix = sanitizeTemporaryPurpose(purpose);
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t allocationId = temporaryDirectoryCounter.fetch_add(1, std::memory_order_relaxed);

            for (std::size_t attempt = 0; attempt < 128; ++attempt)
            {
                const std::filesystem::path candidate = root_ / std::format("{}_{:x}_{:x}_{:x}", prefix, ticks, allocationId, attempt);
                error.clear();
                if (std::filesystem::create_directory(candidate, error))
                {
                    path_ = candidate;
                    return;
                }
                if (error && error != std::errc::file_exists)
                {
                    status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                    return;
                }
            }

            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(std::make_error_code(std::errc::file_exists)));
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error.code()));
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    ScopedTemporaryDirectory::~ScopedTemporaryDirectory() noexcept
    {
        try
        {
            std::error_code error;
            if (!path_.empty())
            {
                static_cast<void>(std::filesystem::remove_all(path_, error));
            }
            if (!root_.empty())
            {
                removeDirectoryIfEmpty(root_);
                removeDirectoryIfEmpty(root_.parent_path());
            }
        }
        catch (...) // NOLINT(bugprone-empty-catch) -- Destruction cannot report best-effort temporary-directory cleanup failure.
        {
        }
    }

    const std::filesystem::path &ScopedTemporaryDirectory::path() const noexcept
    {
        return path_;
    }

    Types::InfrastructureStatus ScopedTemporaryDirectory::status() const noexcept
    {
        return status_;
    }

    ScopedCurrentPath::ScopedCurrentPath(const std::filesystem::path &path) noexcept
    {
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::CurrentPath))
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return;
        }
#endif
        try
        {
            std::error_code error;
            previousPath_ = std::filesystem::current_path(error);
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                previousPath_.clear();
                return;
            }

            std::filesystem::current_path(path, error);
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                previousPath_.clear();
            }
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
            previousPath_.clear();
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed);
            previousPath_.clear();
        }
    }

    ScopedCurrentPath::~ScopedCurrentPath() noexcept
    {
        if (status_.ok())
        {
            try
            {
                std::error_code error;
                std::filesystem::current_path(previousPath_, error);
            }
            catch (...) // NOLINT(bugprone-empty-catch) -- Destruction cannot report best-effort current-path restoration failure.
            {
            }
        }
    }

    const std::filesystem::path &ScopedCurrentPath::previousPath() const noexcept
    {
        return previousPath_;
    }

    Types::InfrastructureStatus ScopedCurrentPath::status() const noexcept
    {
        return status_;
    }

    Types::TextResult readTextFile(const std::filesystem::path &path) noexcept
    {
        Types::TextResult result;
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Read))
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return result;
        }
#endif
        try
        {
            errno = 0;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            const auto endPosition = file.tellg();
            if (endPosition < std::ifstream::pos_type{0})
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }
            if (endPosition == std::ifstream::pos_type{0})
            {
                return result;
            }

            const auto fileSize = static_cast<std::uintmax_t>(endPosition);
            if (fileSize > result.text.max_size())
            {
                result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
                return result;
            }
            if (fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed);
                return result;
            }

            result.text.resize(static_cast<std::size_t>(fileSize));
            file.seekg(0, std::ios::beg);
            if (!file)
            {
                result.text.clear();
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            file.read(result.text.data(), static_cast<std::streamsize>(result.text.size()));
            result.text.resize(static_cast<std::size_t>(file.gcount()));
            if (file.bad())
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (const std::length_error &)
        {
            result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error.code()));
            return result;
        }
        catch (...)
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed);
            return result;
        }
    }

    Types::InfrastructureStatus writeTextFile(const std::filesystem::path &path, std::string_view text) noexcept
    {
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Write))
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
        }
#endif
        try
        {
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return failureStatus(Types::InfrastructureError::InvalidArgument);
            }

            const std::filesystem::path parentPath = path.parent_path();
            if (!parentPath.empty())
            {
                const Types::InfrastructureStatus directoryStatus = createDirectories(parentPath);
                if (!directoryStatus.ok())
                {
                    return directoryStatus;
                }
            }

            errno = 0;
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }

            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();
            if (!file)
            {
                return failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::length_error &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error.code()));
        }
        catch (...)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    Types::BoolResult fileExists(const std::filesystem::path &path) noexcept
    {
        Types::BoolResult result;
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Exists))
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return result;
        }
#endif
        try
        {
            std::error_code error;
            result.value = std::filesystem::exists(path, error);
            if (error)
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (...)
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed);
            return result;
        }
    }

    Types::BoolResult fileContains(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);
        Types::BoolResult result{.status = readResult.status};
        if (result.status.ok())
        {
            result.value = readResult.text.find(text) != std::string::npos;
        }
        return result;
    }

    Types::CountResult countFileOccurrences(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);
        Types::CountResult result{.status = readResult.status};
        if (!result.status.ok() || text.empty())
        {
            return result;
        }

        std::size_t position = 0;
        while ((position = readResult.text.find(text, position)) != std::string::npos)
        {
            ++result.count;
            position += text.size();
        }
        return result;
    }

    Types::InfrastructureStatus createDirectories(const std::filesystem::path &path) noexcept
    {
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::CreateDirectories))
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
        }
#endif
        if (path.empty())
        {
            return {};
        }

        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::create_directories(path, error));
            return error ? failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error)) : Types::InfrastructureStatus{};
        }
        catch (const std::bad_alloc &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    Types::InfrastructureStatus removeIfExists(const std::filesystem::path &path) noexcept
    {
#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Remove))
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
        }
#endif
        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove_all(path, error));
            return error ? failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error)) : Types::InfrastructureStatus{};
        }
        catch (const std::bad_alloc &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    ScopedEnvironmentVariable::ScopedEnvironmentVariable(std::string_view name, std::string_view value) noexcept
    {
        try
        {
            name_.assign(name);
            // Lock only the snapshot-and-mutate operation. Holding a global lock for the full RAII
            // lifetime would deadlock nested guards and would not coordinate external environment APIs.
            std::lock_guard lock(environmentMutex);
            Detail::Platform::EnvironmentReadResult readResult = Detail::Platform::readEnvironmentVariable(name_);
            if (!readResult.status.ok())
            {
                status_ = readResult.status;
                return;
            }
            previousValue_ = std::move(readResult.value);
            status_ = Detail::Platform::setEnvironmentVariableValue(name_, value);
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::EnvironmentFailed);
        }
    }

    ScopedEnvironmentVariable::~ScopedEnvironmentVariable() noexcept
    {
        if (!status_.ok())
        {
            return;
        }

        try
        {
            // Restoration is serialized as one mutation, but another overlapping scope may have
            // changed the same process-global name since this guard was constructed.
            std::lock_guard lock(environmentMutex);
            if (previousValue_)
            {
                static_cast<void>(Detail::Platform::setEnvironmentVariableValue(name_, *previousValue_));
            }
            else
            {
                static_cast<void>(Detail::Platform::unsetEnvironmentVariableValue(name_));
            }
        }
        catch (...) // NOLINT(bugprone-empty-catch) -- Destruction cannot report a best-effort process-state restoration failure.
        {
        }
    }

    Types::InfrastructureStatus ScopedEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    ScopedUnsetEnvironmentVariable::ScopedUnsetEnvironmentVariable(std::string_view name) noexcept
    {
        try
        {
            name_.assign(name);
            // Lock only the snapshot-and-mutate operation. Holding a global lock for the full RAII
            // lifetime would deadlock nested guards and would not coordinate external environment APIs.
            std::lock_guard lock(environmentMutex);
            Detail::Platform::EnvironmentReadResult readResult = Detail::Platform::readEnvironmentVariable(name_);
            if (!readResult.status.ok())
            {
                status_ = readResult.status;
                return;
            }
            previousValue_ = std::move(readResult.value);
            status_ = Detail::Platform::unsetEnvironmentVariableValue(name_);
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::EnvironmentFailed);
        }
    }

    ScopedUnsetEnvironmentVariable::~ScopedUnsetEnvironmentVariable() noexcept
    {
        if (!status_.ok())
        {
            return;
        }

        try
        {
            // Restoration is serialized as one mutation, but another overlapping scope may have
            // changed the same process-global name since this guard was constructed.
            std::lock_guard lock(environmentMutex);
            if (previousValue_)
            {
                static_cast<void>(Detail::Platform::setEnvironmentVariableValue(name_, *previousValue_));
            }
            else
            {
                static_cast<void>(Detail::Platform::unsetEnvironmentVariableValue(name_));
            }
        }
        catch (...) // NOLINT(bugprone-empty-catch) -- Destruction cannot report a best-effort process-state restoration failure.
        {
        }
    }

    Types::InfrastructureStatus ScopedUnsetEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    Types::ManualAnswer promptManualCheck(std::string_view question)
    {
        for (;;)
        {
            std::cout << "[MANUAL] " << question << " [y/n/s]: ";
            std::string answer;
            if (!std::getline(std::cin, answer))
            {
                return Types::ManualAnswer::Skipped;
            }

            if (answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes")
            {
                return Types::ManualAnswer::Yes;
            }

            if (answer == "n" || answer == "N" || answer == "no" || answer == "No")
            {
                return Types::ManualAnswer::No;
            }

            if (answer == "s" || answer == "S" || answer == "skip" || answer == "Skip")
            {
                return Types::ManualAnswer::Skipped;
            }
        }
    }

    void StartGate::wait()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(
            lock,
            [this]
            {
                return open_;
            });
    }

    void StartGate::open()
    {
        {
            std::lock_guard lock(mutex_);
            open_ = true;
        }
        condition_.notify_all();
    }

    void StopFlag::requestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
    }

    bool StopFlag::stopRequested() const noexcept
    {
        return stopRequested_.load(std::memory_order_acquire);
    }
} // namespace GameWIP::TestSupport
