/// @file context.cpp
/// @brief Reporting context, expectations, and timing implementation for TestSupport.

#include "test_support/reporting.h"
#include "test_support/files.h"
#include "test_support/internal/report_sink.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string_view>
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

    /// ------------------------------------------------------------
    /// Reporting result values
    /// ------------------------------------------------------------

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

    /// ------------------------------------------------------------
    /// Timing diagnostics
    /// ------------------------------------------------------------

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

    /// ------------------------------------------------------------
    /// Test context
    /// ------------------------------------------------------------

    Context::Context(std::string_view suiteName, const Types::Reporting::Options &options)
        : suiteName_(suiteName)
        , reportSink_(std::make_shared<Internal::ReportSink>(options))
    {
    }

    Context::Context(std::string_view suiteName, std::shared_ptr<Internal::ReportSink> reportSink)
        : suiteName_(suiteName)
        , reportSink_(std::move(reportSink))
        , ownsReportLifecycle_(false)
    {
    }

    Context::~Context() noexcept
    {
        if(ownsReportLifecycle_)
        {
            try
            {
                static_cast<void>(finalize());
            }
            catch(...)
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
        if(value)
        {
            pass(name);
            return true;
        }

        fail(name, "expected true", location);
        return false;
    }

    bool Context::expectFalse(std::string_view name, bool value, std::source_location location)
    {
        if(!value)
        {
            pass(name);
            return true;
        }

        fail(name, "expected false", location);
        return false;
    }

    bool Context::expectNear(std::string_view name, double expected, double actual, double tolerance, std::source_location location)
    {
        if(tolerance < 0.0)
        {
            fail(name, "tolerance must be non-negative", location);
            return false;
        }

        if(std::fabs(expected - actual) <= tolerance)
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
        if(text.contains(expectedSubstring))
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

        if(result.status.ok() && result.value)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected file '" << path.string() << "' to contain '" << expectedSubstring << "'";

        if(!result.status.ok())
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

        if(result.status.ok() && result.count == expectedCount)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected " << expectedCount << " occurrences of '" << text << "' in file '" << path.string() << "', got " << result.count;

        if(!result.status.ok())
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
        if(!ownsReportLifecycle_)
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
} // namespace GameWIP::TestSupport
