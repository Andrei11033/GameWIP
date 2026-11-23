/// @file runner.cpp
/// @brief Suite runner, sections, and manual-check implementation for TestSupport.

#include "test_support/reporting.h"
#include "test_support/internal/report_sink.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

namespace GameWIP::TestSupport
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    Runner::Runner(Types::Reporting::Options options)
        : reportSink_(std::make_shared<Internal::ReportSink>(std::move(options)))
    {
    }

    Runner::~Runner() noexcept
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
        catch(...)
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
        catch(...) // NOLINT(bugprone-empty-catch) -- Diagnostic-only destructor cannot propagate.
        {
        }
    }

    /// ------------------------------------------------------------
    /// Manual checks
    /// ------------------------------------------------------------

    Types::Reporting::ManualAnswer promptManualCheck(std::string_view question)
    {
        std::string skipReason;
        return promptManualCheck(question, skipReason);
    }

    Types::Reporting::ManualAnswer promptManualCheck(std::string_view question, std::string &skipReason)
    {
        skipReason.clear();

        for(;;)
        {
            std::cout << "[MANUAL] " << question << " [y/n/s reason]: ";
            std::string answer;

            if(!std::getline(std::cin, answer))
            {
                skipReason = "input ended before an answer";
                return Types::Reporting::ManualAnswer::Skipped;
            }

            if(answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes")
            {
                return Types::Reporting::ManualAnswer::Yes;
            }

            if(answer == "n" || answer == "N" || answer == "no" || answer == "No")
            {
                return Types::Reporting::ManualAnswer::No;
            }

            if(answer == "s" || answer == "S" || answer == "skip" || answer == "Skip")
            {
                skipReason = "no skip reason supplied";
                return Types::Reporting::ManualAnswer::Skipped;
            }
            const std::size_t separator = answer.find(' ');
            const std::string_view command = std::string_view{answer}.substr(0, separator);

            if(separator != std::string::npos && (command == "s" || command == "S" || command == "skip" || command == "Skip"))
            {
                const std::size_t reasonStart = answer.find_first_not_of(" \t", separator);
                skipReason = reasonStart == std::string::npos ? "no skip reason supplied" : answer.substr(reasonStart);
                return Types::Reporting::ManualAnswer::Skipped;
            }
        }
    }
} // namespace GameWIP::TestSupport
