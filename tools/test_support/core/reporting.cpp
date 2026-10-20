/// @file reporting.cpp
/// @brief Reporting, expectations, manual-check, and timing implementation for TestSupport.

#include "test_support/reporting.h"
#include "test_support/files.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <utility>

namespace GameWIP::TestSupport
{
    namespace
    {
        [[nodiscard]] std::string makeNameReason(std::string_view name, std::string_view reason)
        {
            std::ostringstream message;
            message << name << ": " << reason;
            return message.str();
        }
    } // namespace

    namespace Detail
    {
        class ReportSink
        {
        public:
            explicit ReportSink(Types::Reporting::Options options)
                : options_(std::move(options))
            {
                status_.reportPath = options_.reportPath;

                if (options_.writeReport && !options_.reportPath.empty())
                {
                    const std::filesystem::path parentPath = options_.reportPath.parent_path();

                    if (!parentPath.empty())
                    {
                        std::error_code error;
                        std::filesystem::create_directories(parentPath, error);

                        if (error)
                        {
                            disableReport(
                                std::string("could not create the report directory: ") + error.message(),
                                {.error = Types::InfrastructureError::FileOperationFailed,
                                 .nativeCode = static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.value()))});
                            return;
                        }
                    }

                    const std::ios::openmode mode = options_.appendReport ? (std::ios::out | std::ios::app) : (std::ios::out | std::ios::trunc);
                    report_.open(options_.reportPath, mode);

                    if (!report_.is_open())
                    {
                        disableReport("could not open the report file", {.error = Types::InfrastructureError::FileOperationFailed});
                        return;
                    }
                    status_.state = Types::Reporting::SinkState::Active;
                }
            }

            ~ReportSink() noexcept
            {
                try
                {
                    static_cast<void>(finalize());
                }
                catch (...)
                {
                    // Destruction is fallback cleanup and cannot report through exceptions.
                    return;
                }
            }

            void write(std::string_view category, std::string_view message)
            {
                write(category, {}, message);
            }

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

                if (status_.state == Types::Reporting::SinkState::Active)
                {
                    report_ << text << '\n';

                    if (options_.flushReportEachLine)
                    {
                        report_.flush();
                    }

                    if (!report_ || !report_.is_open())
                    {
                        disableReport("report output failed while writing", {.error = Types::InfrastructureError::FileOperationFailed});
                    }
                }
            }

            void flush()
            {
                std::lock_guard lock(mutex_);
                flushLocked();
            }

            [[nodiscard]] Types::Reporting::SinkStatus status() const
            {
                std::lock_guard lock(mutex_);
                return status_;
            }

            [[nodiscard]] Types::Reporting::SinkStatus finalize()
            {
                std::lock_guard lock(mutex_);

                if (status_.state != Types::Reporting::SinkState::Active)
                {
                    return status_;
                }

                if (!flushLocked())
                {
                    return status_;
                }

                report_.close();

                if (!report_)
                {
                    disableReport("report output failed while closing", {.error = Types::InfrastructureError::FileOperationFailed});
                    return status_;
                }

                status_.state = Types::Reporting::SinkState::Finalized;
                return status_;
            }

        private:
            [[nodiscard]] bool shouldWriteToConsole(std::string_view category) const noexcept
            {
                if (!options_.writeConsole)
                {
                    return false;
                }

                if (options_.consoleVerbosity == Types::Reporting::ConsoleVerbosity::Full)
                {
                    return true;
                }

                const bool isActionable = category == "FAIL" || category == "SKIP" || category == "MANUAL";

                if (options_.consoleVerbosity == Types::Reporting::ConsoleVerbosity::Minimal)
                {
                    return isActionable;
                }

                return isActionable || category == "SUMMARY" || category == "RESULT";
            }

            bool flushLocked()
            {
                if (status_.state != Types::Reporting::SinkState::Active)
                {
                    return status_.state != Types::Reporting::SinkState::Failed;
                }

                report_.flush();

                if (!report_)
                {
                    disableReport("report output failed while flushing", {.error = Types::InfrastructureError::FileOperationFailed});
                    return false;
                }

                return true;
            }

            void disableReport(std::string_view reason, Types::InfrastructureStatus failure)
            {
                status_.state = Types::Reporting::SinkState::Failed;
                status_.failureStatus = failure;
                report_.close();

                if (!reportFailureReported_)
                {
                    std::cerr << "[TEST REPORT] " << reason << ": " << options_.reportPath.string() << '\n';
                    reportFailureReported_ = true;
                }
            }

            Types::Reporting::Options options_;
            std::ofstream report_;
            mutable std::mutex mutex_;
            Types::Reporting::SinkStatus status_;
            bool reportFailureReported_ = false;
        };
    } // namespace Detail

    // ------------------------------------------------------------
    // Reporting result values
    // ------------------------------------------------------------

    std::size_t Types::Reporting::Summary::total() const noexcept
    {
        return passed + failed + skipped;
    }

    bool Types::Reporting::Summary::ok() const noexcept
    {
        return failed == 0;
    }

    bool Types::Reporting::SuiteResult::ok() const noexcept
    {
        return summary.ok();
    }

    // ------------------------------------------------------------
    // Timing diagnostics
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Test context
    // ------------------------------------------------------------

    Context::Context(std::string_view suiteName, const Types::Reporting::Options &options)
        : suiteName_(suiteName)
        , reportSink_(std::make_shared<Detail::ReportSink>(options))
    {
    }

    Context::Context(std::string_view suiteName, std::shared_ptr<Detail::ReportSink> reportSink)
        : suiteName_(suiteName)
        , reportSink_(std::move(reportSink))
        , ownsReportLifecycle_(false)
    {
    }

    Context::~Context() noexcept
    {
        if (ownsReportLifecycle_)
        {
            try
            {
                static_cast<void>(finalize());
            }
            catch (...)
            {
                // Destruction is fallback cleanup and cannot report through exceptions.
                return;
            }
        }
    }

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
        if (text.contains(expectedSubstring))
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

    Types::Reporting::Summary Context::result() const noexcept
    {
        std::lock_guard lock(mutex_);
        return summary_;
    }

    bool Context::ok() const noexcept
    {
        return result().ok();
    }

    Types::Reporting::SinkStatus Context::reportStatus() const
    {
        return reportSink_->status();
    }

    Types::Reporting::SinkStatus Context::finalize()
    {
        if (!ownsReportLifecycle_)
        {
            return reportStatus();
        }

        return reportSink_->finalize();
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

    // ------------------------------------------------------------
    // Test runner
    // ------------------------------------------------------------

    Runner::Runner(Types::Reporting::Options options)
        : reportSink_(std::make_shared<Detail::ReportSink>(std::move(options)))
    {
    }

    Runner::~Runner() noexcept
    {
        try
        {
            static_cast<void>(finalize());
        }
        catch (...)
        {
            // Destruction is fallback cleanup and cannot report through exceptions.
            return;
        }
    }

    void Runner::info(std::string_view message)
    {
        reportSink_->write("INFO", message);
    }

    void Runner::summary(std::string_view message)
    {
        reportSink_->write("SUMMARY", message);
    }

    Types::Reporting::Summary Runner::result() const noexcept
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

    Types::Reporting::SinkStatus Runner::reportStatus() const
    {
        return reportSink_->status();
    }

    Types::Reporting::SinkStatus Runner::finalize()
    {
        try
        {
            return reportSink_->finalize();
        }
        catch (...)
        {
            Types::Reporting::SinkStatus status = reportSink_->status();
            status.state = Types::Reporting::SinkState::Failed;
            status.failureStatus = {.error = Types::InfrastructureError::FileOperationFailed};
            return status;
        }
    }

    void Runner::recordSuiteResult(const Types::Reporting::SuiteResult &result)
    {
        {
            std::lock_guard lock(mutex_);
            summary_.passed += result.summary.passed;
            summary_.failed += result.summary.failed;
            summary_.skipped += result.summary.skipped;
        }

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
        catch (...) // NOLINT(bugprone-empty-catch) -- Diagnostic-only destructor cannot propagate.
        {
        }
    }

    // ------------------------------------------------------------
    // Manual checks
    // ------------------------------------------------------------

    Types::Reporting::ManualAnswer promptManualCheck(std::string_view question)
    {
        std::string skipReason;
        return promptManualCheck(question, skipReason);
    }

    Types::Reporting::ManualAnswer promptManualCheck(std::string_view question, std::string &skipReason)
    {
        skipReason.clear();

        for (;;)
        {
            std::cout << "[MANUAL] " << question << " [y/n/s reason]: ";
            std::string answer;

            if (!std::getline(std::cin, answer))
            {
                skipReason = "input ended before an answer";
                return Types::Reporting::ManualAnswer::Skipped;
            }

            if (answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes")
            {
                return Types::Reporting::ManualAnswer::Yes;
            }

            if (answer == "n" || answer == "N" || answer == "no" || answer == "No")
            {
                return Types::Reporting::ManualAnswer::No;
            }

            if (answer == "s" || answer == "S" || answer == "skip" || answer == "Skip")
            {
                skipReason = "no skip reason supplied";
                return Types::Reporting::ManualAnswer::Skipped;
            }
            const std::size_t separator = answer.find(' ');
            const std::string_view command = std::string_view{answer}.substr(0, separator);

            if (separator != std::string::npos && (command == "s" || command == "S" || command == "skip" || command == "Skip"))
            {
                const std::size_t reasonStart = answer.find_first_not_of(" \t", separator);
                skipReason = reasonStart == std::string::npos ? "no skip reason supplied" : answer.substr(reasonStart);
                return Types::Reporting::ManualAnswer::Skipped;
            }
        }
    }
} // namespace GameWIP::TestSupport
