/// @file runner_helpers.h
/// @brief Shared console output, reporting setup, and finalization for validation modules.

#pragma once

#include "test_support/test_support.h"
#include "terminal/output.h"

#include <array>
#include <iostream>
#include <string_view>

namespace GameWIP::Test::RunnerHelpers
{
    namespace Detail
    {
        enum class ReportStyle
        {
            None,
            Green,
            Red,
            Yellow,
            Cyan,
            Gray,
            Blue,
            Magenta,
            Result,
        };

        struct ReportCategoryStyle
        {
            std::string_view category;
            ReportStyle style;
        };

        constexpr std::array kReportCategoryStyles{
            ReportCategoryStyle{"PASS", ReportStyle::Green},
            ReportCategoryStyle{"FAIL", ReportStyle::Red},
            ReportCategoryStyle{"SKIP", ReportStyle::Yellow},
            ReportCategoryStyle{"MANUAL", ReportStyle::Cyan},
            ReportCategoryStyle{"SUMMARY", ReportStyle::Cyan},
            ReportCategoryStyle{"INFO", ReportStyle::Gray},
            ReportCategoryStyle{"METRIC", ReportStyle::Blue},
            ReportCategoryStyle{"STRESS", ReportStyle::Magenta},
            ReportCategoryStyle{"RESULT", ReportStyle::Result},
        };

        [[nodiscard]] inline ReportStyle reportStyleFor(std::string_view category) noexcept
        {
            for(const ReportCategoryStyle &entry : kReportCategoryStyles)
            {
                if(entry.category == category)
                {
                    return entry.style;
                }
            }

            return ReportStyle::None;
        }
    } // namespace Detail

    /// @brief Writes one validation report line with category-based styling when supported.
    [[nodiscard]] inline bool writeValidationConsoleLine(std::string_view category, std::string_view line) noexcept
    {
        using GameWIP::Terminal::Types::Output::Segment;
        using GameWIP::Terminal::Types::Style::BasicColor;
        using GameWIP::Terminal::Types::Style::Request;

        const Detail::ReportStyle reportStyle = Detail::reportStyleFor(category);
        Request style;
        switch(reportStyle)
        {
        case Detail::ReportStyle::None:
            break;
        case Detail::ReportStyle::Green:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightGreen);
            break;
        case Detail::ReportStyle::Red:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightRed);
            break;
        case Detail::ReportStyle::Yellow:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightYellow);
            break;
        case Detail::ReportStyle::Cyan:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightCyan);
            break;
        case Detail::ReportStyle::Gray:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightBlack);
            break;
        case Detail::ReportStyle::Blue:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightBlue);
            break;
        case Detail::ReportStyle::Magenta:
            style.foreground = GameWIP::Terminal::basicColor(BasicColor::BrightMagenta);
            break;
        case Detail::ReportStyle::Result:
        {
            const BasicColor color = line.contains("failed=0") ? BasicColor::BrightGreen : BasicColor::BrightRed;
            style.foreground = GameWIP::Terminal::basicColor(color);
            break;
        }
        }

        try
        {
            GameWIP::Terminal::Types::Output::SegmentOptions options;
            options.styleMode = GameWIP::Terminal::Types::Style::Mode::Auto;
            options.appendLineEnding = true;
            options.lineEnding = GameWIP::Terminal::Types::Output::LineEnding::Native;

            const Segment segment =
                reportStyle == Detail::ReportStyle::None ? GameWIP::Terminal::textSegment(line) : GameWIP::Terminal::styledTextSegment(line, style);
            const std::array<Segment, 1> segments{segment};
            return GameWIP::Terminal::writeSegments(GameWIP::Terminal::Types::Output::Stream::Stdout, segments, options).ok();
        }
        catch(...)
        {
            return false;
        }
    }

    /// @brief Writes one runner-owned status line and diagnoses Terminal output failures.
    inline void writeValidationConsoleLineOrDiagnose(std::string_view category, std::string_view line)
    {
        if(!writeValidationConsoleLine(category, line))
        {
            std::cerr << "[TEST OUTPUT] validation console writer failed; report line was not written.\n";
        }
    }

    /// @brief Maps module reporting policy into the shared TestSupport sink options.
    /// @tparam Options Module options exposing reporting and console fields.
    /// @param options Module reporting policy.
    /// @return Corresponding shared report sink options.
    template <typename Options> [[nodiscard]] inline TestSupport::Types::Reporting::Options makeReportingOptions(const Options &options)
    {
        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.consoleWriter = &writeValidationConsoleLine;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;
        return reportOptions;
    }

    /// @brief Finalizes reporting and returns the recorded correctness result.
    /// @param runner Completed module runner.
    /// @return The runner's test exit code.
    /// @note Report sink failures are diagnosed without changing the test result.
    [[nodiscard]] inline int finalizeRunner(TestSupport::Runner &runner)
    {
        const int exitCode = runner.exitCode();
        const TestSupport::Types::Reporting::SinkStatus reportStatus = runner.finalize();

        if(!reportStatus.failureStatus.ok())
        {
            std::cerr << "[TEST INFRA] report sink failed: " << TestSupport::formatInfrastructureStatus(reportStatus.failureStatus) << '\n';
        }

        return exitCode;
    }
} // namespace GameWIP::Test::RunnerHelpers
