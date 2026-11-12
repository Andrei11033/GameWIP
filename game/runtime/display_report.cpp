/// @file display_report.cpp
/// @brief Startup display inspection and diagnostic reporting for the executable runtime.

#include "runtime/display_report.h"

#include "runtime/logging.h"
#include "runtime/profiling.h"

#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    namespace Display = GameWIP::Desktop::Types::Display;

    /// @brief Returns the display name used in logs for a color space.
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

    /// @brief Formats a refresh rate in Hz, omitting the fraction when it is whole.
    [[nodiscard]] std::string formatRefreshRate(std::uint32_t millihertz)
    {
        return millihertz % 1000 == 0 ? std::format("{}", millihertz / 1000) : std::format("{}.{:03}", millihertz / 1000, millihertz % 1000);
    }

    /// @brief Appends one line per resolution listing every refresh rate that resolution supports.
    void appendSupportedModes(std::string &report, const std::vector<Display::Mode> &modes)
    {
        const Display::Mode *previous = nullptr;

        for(const auto &mode : modes)
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
    }

    /// @brief Formats the inventory, active mode, and color capabilities of one display.
    [[nodiscard]] std::string formatDisplayReport(
        const Display::Info &monitor,
        const GameWIP::Desktop::Types::Result<Display::Mode> &activeMode,
        const GameWIP::Desktop::Types::Result<std::vector<Display::Mode>> &supportedModes,
        const GameWIP::Desktop::Types::Result<Display::ColorInfo> &colorInfo)
    {
        std::string report;
        std::format_to(
            std::back_inserter(report),
            "Display '{}'{} at ({}, {})",
            monitor.name,
            monitor.primary ? " [primary]" : "",
            monitor.bounds.position.x,
            monitor.bounds.position.y);

        if(supportedModes.ok())
        {
            std::format_to(std::back_inserter(report), " has {} supported mode(s)", supportedModes->size());
        }
        else
        {
            std::format_to(std::back_inserter(report), "\n  mode query failed: {}", supportedModes.status.message);
        }

        if(activeMode.ok())
        {
            std::format_to(
                std::back_inserter(report),
                "\n  active: {}x{} @ {} Hz, {} bpp{}",
                activeMode->resolution.width,
                activeMode->resolution.height,
                formatRefreshRate(activeMode->refreshRateMillihertz),
                activeMode->bitsPerPixel,
                activeMode->interlaced ? ", interlaced" : "");
        }
        else
        {
            std::format_to(std::back_inserter(report), "\n  active mode query failed: {}", activeMode.status.message);
        }

        if(colorInfo.ok())
        {
            std::format_to(
                std::back_inserter(report),
                "\n  color: {}, HDR supported={}, HDR enabled={}, WCG supported={}, {} bits/channel, "
                "luminance min/peak/full-frame={:.3f}/{:.1f}/{:.1f} nits, SDR white={:.1f} nits",
                colorSpaceName(colorInfo->activeColorSpace),
                colorInfo->hdrSupported,
                colorInfo->hdrEnabled,
                colorInfo->wideColorGamutSupported,
                colorInfo->bitsPerColorChannel.value_or(0),
                colorInfo->minimumLuminanceNits.value_or(0.0F),
                colorInfo->maximumLuminanceNits.value_or(0.0F),
                colorInfo->maximumFullFrameLuminanceNits.value_or(0.0F),
                colorInfo->sdrWhiteLevelNits.value_or(0.0F));
        }
        else
        {
            std::format_to(std::back_inserter(report), "\n  HDR/color query failed: {}", colorInfo.status.message);
        }

        return report;
    }

    /// @brief Formats the grouped supported-mode list, or an empty string when none can be queried.
    [[nodiscard]] std::string formatSupportedModeReport(
        const Display::Info &monitor,
        const GameWIP::Desktop::Types::Result<std::vector<Display::Mode>> &supportedModes)
    {
        if(!supportedModes.ok() || supportedModes->empty())
        {
            return {};
        }

        std::string report = std::format("Display '{}' supported modes:", monitor.name);
        appendSupportedModes(report, *supportedModes);
        return report;
    }
} // namespace

namespace GameWIP::Game
{
    Desktop::Types::Result<std::vector<Desktop::Types::Display::Info>> inspectDisplays()
    {
        GAMEWIP_PROFILE_ZONE("Enumerate displays, display modes, and HDR state", Profiling::Color::kInitialization);
        auto monitors = Desktop::Display::getMonitors();

        if(!monitors.ok())
        {
            return monitors;
        }
        Logger::debug(Logging::Area::Display, "Enumerated {} connected display(s)", Logger::number(monitors->size()));

        if(!Logger::shouldLog(Logger::Types::Level::Debug, Logging::Area::Display))
        {
            return monitors;
        }

        for(const Desktop::Types::Display::Info &monitor : *monitors)
        {
            const auto activeMode = Desktop::Display::getCurrentMode(monitor.id);
            const auto supportedModes = Desktop::Display::getModes(monitor.id);
            const auto colorInfo = Desktop::Display::getColorInfo(monitor.id);

            Logger::debug(Logging::Area::Display, "{}", formatDisplayReport(monitor, activeMode, supportedModes, colorInfo));

            // The full mode list is long, so it is kept at Debug to leave normal startup output short.
            if(const std::string modes = formatSupportedModeReport(monitor, supportedModes); !modes.empty())
            {
                Logger::debug(Logging::Area::Display, "{}", modes);
            }
        }

        return monitors;
    }
} // namespace GameWIP::Game
