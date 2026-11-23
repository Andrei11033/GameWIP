/// @file input_reports.cpp
/// @brief Logs bounded raw Input report previews at Trace.

#include "input_reports.h"

#include "input_event_format.h"
#include "logger/logging.h"
#include "logger/message_format.h"
#include "runtime/logging.h"
#include "runtime/platform/input_reports.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <utility>

namespace GameWIP::Game::RuntimeDiagnostics
{
    /// ------------------------------------------------------------
    /// Raw report previews
    /// ------------------------------------------------------------

    namespace
    {
        namespace Logger = GameWIP::Logger;
        namespace Types = GameWIP::Input::Types;
        namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
        namespace Platform = GameWIP::Game::Runtime::Platform;
        using LogArea = GameWIP::Game::Logging::Area;
        constexpr std::size_t kPreviewByteCount = 32;

        /// @brief Formats a bounded uppercase byte preview.
        [[nodiscard]] std::string bytePreview(std::span<const std::byte> bytes)
        {
            const std::size_t previewCount = std::min(bytes.size(), kPreviewByteCount);
            std::string preview;
            preview.reserve(previewCount * 3);

            for(std::size_t index = 0; index < previewCount; ++index)
            {
                if(!preview.empty())
                {
                    preview += ' ';
                }
                preview += std::format("{:02X}", std::to_integer<unsigned int>(bytes[index]));
            }

            return preview;
        }

        void logRawReportPreview(const Types::Frame &frame, const Types::RawReport &report)
        {
            const std::string preview = bytePreview(report.bytes);
            const std::string_view backend = Format::backend(report.backend);
            Logger::trace(
                LogArea::Input,
                Logger::record()
                    .field("capture_sequence", frame.sequence())
                    .field("device_system_instance", report.device.instance)
                    .field("device_slot", report.device.slot)
                    .field("device_generation", report.device.generation)
                    .field("backend", backend)
                    .field("report_size", report.bytes.size())
                    .field("report_preview_truncated", report.bytes.size() > kPreviewByteCount),
                "Raw input report: {} size={} bytes=[{}] [device #{}]",
                Logger::code(backend),
                Logger::number(report.bytes.size()),
                Logger::code(preview),
                Logger::number(report.device.slot));
        }
    } // namespace

    void logRawInputReports(const Types::Frame &frame)
    {
        // Log each device and backend's first report, keeping high-rate sources bounded.
        constexpr std::size_t kMaximumReportedDevices = 64;
        std::array<std::pair<Types::DeviceId, Types::BackendKind>, kMaximumReportedDevices> reportedSources{};
        std::size_t reportedSourceCount = 0;

        for(const Types::RawReport &report : frame.rawReports())
        {
            if(report.bytes.empty())
            {
                continue;
            }

            const std::pair source{report.device, report.backend};
            const auto reported = std::span{reportedSources}.first(reportedSourceCount);
            const bool alreadyReported = std::ranges::find(reported, source) != reported.end();
            if(!alreadyReported && reportedSourceCount < reportedSources.size())
            {
                reportedSources.at(reportedSourceCount++) = source;
                logRawReportPreview(frame, report);
            }

            // Let the selected platform decode every report it recognizes.
            Platform::logRawInputDetails(report, frame.sequence());
        }
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
