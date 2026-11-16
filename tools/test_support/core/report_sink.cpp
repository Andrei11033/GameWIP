/// @file report_sink.cpp
/// @brief Report-file and console output for TestSupport.

#include "test_support/internal/report_sink.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace GameWIP::TestSupport::Detail
{
    ReportSink::ReportSink(Types::Reporting::Options options)
        : options_(std::move(options))
    {
        status_.reportPath = options_.reportPath;

        // Prepare the optional report file.
        if(options_.writeReport && !options_.reportPath.empty())
        {
            const std::filesystem::path parentPath = options_.reportPath.parent_path();

            if(!parentPath.empty())
            {
                std::error_code error;
                std::filesystem::create_directories(parentPath, error);

                if(error)
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

            if(!report_.is_open())
            {
                disableReport("could not open the report file", {.error = Types::InfrastructureError::FileOperationFailed});
                return;
            }

            status_.state = Types::Reporting::SinkState::Active;
        }
    }

    ReportSink::~ReportSink() noexcept
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

    void ReportSink::write(std::string_view category, std::string_view message)
    {
        write(category, {}, message);
    }

    void ReportSink::write(std::string_view category, std::string_view suiteName, std::string_view message)
    {
        std::ostringstream line;
        line << '[' << category << ']';

        if(!suiteName.empty())
        {
            line << " [" << suiteName << ']';
        }

        if(!message.empty())
        {
            line << ' ' << message;
        }

        const std::string text = line.str();
        std::lock_guard lock(mutex_);

        // Write enabled console output.
        if(shouldWriteToConsole(category))
        {
            if(options_.consoleWriter == nullptr)
            {
                std::cout << text << '\n';
            }
            else if(!options_.consoleWriter(category, text))
            {
                std::cerr << "[TEST OUTPUT] console writer failed; report line was not written.\n";
            }
        }

        // Write the optional report file.
        if(status_.state == Types::Reporting::SinkState::Active)
        {
            report_ << text << '\n';

            if(options_.flushReportEachLine)
            {
                report_.flush();
            }

            if(!report_ || !report_.is_open())
            {
                disableReport("report output failed while writing", {.error = Types::InfrastructureError::FileOperationFailed});
            }
        }
    }

    void ReportSink::flush()
    {
        std::lock_guard lock(mutex_);
        static_cast<void>(flushLocked());
    }

    Types::Reporting::SinkStatus ReportSink::status() const
    {
        std::lock_guard lock(mutex_);
        return status_;
    }

    Types::Reporting::SinkStatus ReportSink::finalize()
    {
        std::lock_guard lock(mutex_);

        if(status_.state != Types::Reporting::SinkState::Active)
        {
            return status_;
        }

        if(!flushLocked())
        {
            return status_;
        }

        report_.close();

        if(!report_)
        {
            disableReport("report output failed while closing", {.error = Types::InfrastructureError::FileOperationFailed});
            return status_;
        }

        status_.state = Types::Reporting::SinkState::Finalized;
        return status_;
    }

    bool ReportSink::shouldWriteToConsole(std::string_view category) const noexcept
    {
        if(!options_.writeConsole)
        {
            return false;
        }

        if(options_.consoleVerbosity == Types::Reporting::ConsoleVerbosity::Full)
        {
            return true;
        }

        const bool isActionable = category == "FAIL" || category == "SKIP" || category == "MANUAL";

        if(options_.consoleVerbosity == Types::Reporting::ConsoleVerbosity::Minimal)
        {
            return isActionable;
        }

        return isActionable || category == "SUMMARY" || category == "RESULT";
    }

    bool ReportSink::flushLocked()
    {
        if(status_.state != Types::Reporting::SinkState::Active)
        {
            return status_.state != Types::Reporting::SinkState::Failed;
        }

        report_.flush();

        if(!report_)
        {
            disableReport("report output failed while flushing", {.error = Types::InfrastructureError::FileOperationFailed});
            return false;
        }

        return true;
    }

    void ReportSink::disableReport(std::string_view reason, Types::InfrastructureStatus failure)
    {
        status_.state = Types::Reporting::SinkState::Failed;
        status_.failureStatus = failure;
        report_.close();

        if(!reportFailureReported_)
        {
            std::cerr << "[TEST REPORT] " << reason << ": " << options_.reportPath.string() << '\n';
            reportFailureReported_ = true;
        }
    }
} // namespace GameWIP::TestSupport::Detail
