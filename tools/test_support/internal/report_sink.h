/// @file report_sink.h
/// @brief Private report-output sink shared by TestSupport reporting sources.

#pragma once

#include "test_support/reporting.h"

#include <fstream>
#include <mutex>
#include <string_view>

namespace GameWIP::TestSupport::Internal
{
    class ReportSink
    {
    public:
        explicit ReportSink(Types::Reporting::Options options);
        ~ReportSink() noexcept;

        void write(std::string_view category, std::string_view message);
        void write(std::string_view category, std::string_view suiteName, std::string_view message);
        void flush();

        [[nodiscard]] Types::Reporting::SinkStatus status() const;
        [[nodiscard]] Types::Reporting::SinkStatus finalize();

    private:
        [[nodiscard]] bool shouldWriteToConsole(std::string_view category) const noexcept;
        [[nodiscard]] bool flushLocked();
        void disableReport(std::string_view reason, Types::Status failure);

        // Sink configuration
        Types::Reporting::Options options_;

        // Report output and lifecycle
        std::ofstream report_;
        mutable std::mutex mutex_;
        Types::Reporting::SinkStatus status_;
        bool reportFailureReported_ = false;
    };
} // namespace GameWIP::TestSupport::Internal
