/// @file display_report.cpp
/// @brief Startup display inspection and diagnostic reporting for the executable runtime.

#include "display_report.h"

#include "runtime/logging.h"
#include "runtime/profiling.h"

#include "io/status.h"
#include "logger/logging.h"
#include "logger/message_format.h"

#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace Display = GameWIP::Desktop::Types::Display;
    namespace Logger = GameWIP::Logger;
    namespace Logging = GameWIP::Game::Logging;

    // ------------------------------------------------------------
    // Display names and mode formatting
    // ------------------------------------------------------------

    [[nodiscard]] constexpr std::string_view colorSpaceName(Display::ColorSpace colorSpace) noexcept
    {
        switch(colorSpace)
        {
        case Display::ColorSpace::Srgb:
            return "sRGB/SDR";
        case Display::ColorSpace::WideColorGamut:
            return "wide-color SDR";
        case Display::ColorSpace::Hdr10Pq:
            return "HDR10/PQ";
        case Display::ColorSpace::Unknown:
        default:
            return "unknown";
        }
    }

    [[nodiscard]] std::string formatRefreshRate(std::uint32_t millihertz)
    {
        return millihertz % 1000 == 0 ? std::format("{}", millihertz / 1000) : std::format("{}.{:03}", millihertz / 1000, millihertz % 1000);
    }

    [[nodiscard]] std::string formatMode(const Display::Mode &mode)
    {
        return std::format("{}x{}@{}Hz", mode.resolution.width, mode.resolution.height, formatRefreshRate(mode.refreshRateMillihertz));
    }

    [[nodiscard]] std::string formatSupportedModes(const std::vector<Display::Mode> &modes)
    {
        std::string report;
        const Display::Mode *previous = nullptr;
        for(const Display::Mode &mode : modes)
        {
            const bool sameGroup = previous != nullptr && previous->resolution.width == mode.resolution.width &&
                                   previous->resolution.height == mode.resolution.height && previous->bitsPerPixel == mode.bitsPerPixel &&
                                   previous->interlaced == mode.interlaced;
            if(sameGroup)
            {
                std::format_to(std::back_inserter(report), ", {}", formatRefreshRate(mode.refreshRateMillihertz));
            }
            else
            {
                if(previous != nullptr)
                {
                    report += " Hz";
                }
                std::format_to(
                    std::back_inserter(report),
                    "\n    {}x{} {} bpp{} @ {}",
                    mode.resolution.width,
                    mode.resolution.height,
                    mode.bitsPerPixel,
                    mode.interlaced ? " interlaced" : "",
                    formatRefreshRate(mode.refreshRateMillihertz));
            }
            previous = &mode;
        }

        if(previous != nullptr)
        {
            report += " Hz";
        }
        return report;
    }

    // ------------------------------------------------------------
    // Optional display query fields
    // ------------------------------------------------------------

    void logDisplayQueryFailure(Display::MonitorId monitor, std::string_view query, const GameWIP::IO::Types::Status &status)
    {
        const std::string_view message = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        Logger::warn(
            Logging::Area::Display,
            Logger::record()
                .field("monitor_id", monitor.value)
                .field("query", query)
                .field("error_code", GameWIP::IO::errorCodeName(status.code))
                .field("native_code", status.nativeCode)
                .field("status_message", message),
            "Display query failed");
    }

    void addModeFields(
        Logger::Record &record,
        Display::MonitorId monitor,
        const GameWIP::Desktop::Types::Result<Display::Mode> &activeMode,
        const GameWIP::Desktop::Types::Result<std::vector<Display::Mode>> &supportedModes)
    {
        if(activeMode.ok())
        {
            record.field("active_mode", formatMode(*activeMode))
                .field("active_bits_per_pixel", activeMode->bitsPerPixel)
                .field("active_interlaced", activeMode->interlaced);
        }
        else
        {
            logDisplayQueryFailure(monitor, "active_mode", activeMode.status);
        }

        if(supportedModes.ok())
        {
            record.field("mode_count", supportedModes->size());
        }
        else
        {
            logDisplayQueryFailure(monitor, "supported_modes", supportedModes.status);
        }
    }

    void addColorFields(
        Logger::Record &record,
        Display::MonitorId monitor,
        const GameWIP::Desktop::Types::Result<Display::ColorInfo> &colorInfo,
        bool detailed)
    {
        if(!colorInfo.ok())
        {
            logDisplayQueryFailure(monitor, "color_info", colorInfo.status);
            return;
        }

        record.field("color_space", colorSpaceName(colorInfo->activeColorSpace)).field("hdr", colorInfo->hdrEnabled);
        if(!detailed)
        {
            return;
        }

        record.field("hdr_supported", colorInfo->hdrSupported).field("wide_color_gamut_supported", colorInfo->wideColorGamutSupported);
        if(colorInfo->bitsPerColorChannel.has_value())
        {
            record.field("bits_per_channel", *colorInfo->bitsPerColorChannel);
        }

        if(colorInfo->minimumLuminanceNits.has_value())
        {
            record.field("minimum_luminance_nits", *colorInfo->minimumLuminanceNits);
        }

        if(colorInfo->maximumLuminanceNits.has_value())
        {
            record.field("maximum_luminance_nits", *colorInfo->maximumLuminanceNits);
        }

        if(colorInfo->maximumFullFrameLuminanceNits.has_value())
        {
            record.field("maximum_full_frame_luminance_nits", *colorInfo->maximumFullFrameLuminanceNits);
        }

        if(colorInfo->sdrWhiteLevelNits.has_value())
        {
            record.field("sdr_white_level_nits", *colorInfo->sdrWhiteLevelNits);
        }
    }

    // ------------------------------------------------------------
    // Debug and Trace reports
    // ------------------------------------------------------------

    void logDisplaySummary(
        const Display::Info &monitor,
        const GameWIP::Desktop::Types::Result<Display::Mode> &activeMode,
        const GameWIP::Desktop::Types::Result<std::vector<Display::Mode>> &supportedModes,
        const GameWIP::Desktop::Types::Result<Display::ColorInfo> &colorInfo)
    {
        Logger::Record record = Logger::record();
        record.field("primary", monitor.primary);
        addModeFields(record, monitor.id, activeMode, supportedModes);
        addColorFields(record, monitor.id, colorInfo, false);

        Logger::debug(
            Logging::Area::Display,
            record,
            "Display '{}' (id {}, {}x{})",
            Logger::quoted(monitor.name),
            Logger::number(monitor.id.value),
            Logger::number(monitor.bounds.size.width),
            Logger::number(monitor.bounds.size.height));
    }

    void logDisplayTrace(
        const Display::Info &monitor,
        const GameWIP::Desktop::Types::Result<Display::Mode> &activeMode,
        const GameWIP::Desktop::Types::Result<std::vector<Display::Mode>> &supportedModes,
        const GameWIP::Desktop::Types::Result<Display::ColorInfo> &colorInfo)
    {
        Logger::Record record = Logger::record();
        record.field("monitor_id", monitor.id.value)
            .field("name", monitor.name)
            .field("primary", monitor.primary)
            .field("left", monitor.bounds.position.x)
            .field("top", monitor.bounds.position.y)
            .field("width", monitor.bounds.size.width)
            .field("height", monitor.bounds.size.height)
            .field("scale_x", monitor.contentScale.x)
            .field("scale_y", monitor.contentScale.y)
            .field("work_left", monitor.workArea.position.x)
            .field("work_top", monitor.workArea.position.y)
            .field("work_width", monitor.workArea.size.width)
            .field("work_height", monitor.workArea.size.height)
            .field("dpi_x", monitor.effectiveDpi.x)
            .field("dpi_y", monitor.effectiveDpi.y);
        if(monitor.physicalWidthMillimeters != 0)
        {
            record.field("physical_width_mm", monitor.physicalWidthMillimeters);
        }

        if(monitor.physicalHeightMillimeters != 0)
        {
            record.field("physical_height_mm", monitor.physicalHeightMillimeters);
        }
        addModeFields(record, monitor.id, activeMode, supportedModes);
        addColorFields(record, monitor.id, colorInfo, true);

        std::string report = std::format("Display '{}'", Logger::quoted(monitor.name));
        if(supportedModes.ok() && !supportedModes->empty())
        {
            report += "\n  Supported modes:";
            report += formatSupportedModes(*supportedModes);
        }
        Logger::trace(Logging::Area::Display, record, "{}", report);
    }
} // namespace

namespace GameWIP::Game
{
    // ------------------------------------------------------------
    // Display enumeration
    // ------------------------------------------------------------

    Desktop::Types::Result<std::vector<Desktop::Types::Display::Info>> inspectDisplays()
    {
        GAMEWIP_PROFILE_ZONE("Enumerate displays, display modes, and HDR state", Profiling::Color::kInitialization);
        auto monitors = Desktop::Display::getMonitors();
        if(!monitors.ok())
        {
            return monitors;
        }

        Logger::debug(Logging::Area::Display, Logger::record().field("connected_display_count", monitors->size()), "Connected displays enumerated");
        const bool logDebug = Logger::shouldLog(Logger::Types::Level::Debug, Logging::Area::Display);
        const bool logTrace = Logger::shouldLog(Logger::Types::Level::Trace, Logging::Area::Display);
        if(!logDebug && !logTrace)
        {
            return monitors;
        }

        for(const Desktop::Types::Display::Info &monitor : *monitors)
        {
            const auto activeMode = Desktop::Display::getCurrentMode(monitor.id);
            const auto supportedModes = Desktop::Display::getModes(monitor.id);
            const auto colorInfo = Desktop::Display::getColorInfo(monitor.id);
            if(logTrace)
            {
                logDisplayTrace(monitor, activeMode, supportedModes, colorInfo);
            }
            else
            {
                logDisplaySummary(monitor, activeMode, supportedModes, colorInfo);
            }
        }

        return monitors;
    }
} // namespace GameWIP::Game
