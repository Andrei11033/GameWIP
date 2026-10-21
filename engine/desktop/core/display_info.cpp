/// @file display_info.cpp
/// @brief Rich monitor and display-color inspection for Window.

#include "desktop/display_info.h"

#include "desktop/internal/window_platform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Converts native color metadata into a sanitized portable snapshot.
    Types::Display::ColorInfo makeDisplayColorInfo(Types::Display::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept
    {
        const auto reliableLuminance = [](float value) noexcept -> std::optional<float>
        {
            return std::isfinite(value) && value > 0.0F ? std::optional<float>{value} : std::nullopt;
        };

        return {
            .monitor = monitor,
            .activeColorSpace = snapshot.activeColorSpace,
            .wideColorGamutSupported = snapshot.wideColorGamutSupported,
            .hdrSupported = snapshot.hdrSupported || snapshot.hdrEnabled,
            .hdrEnabled = snapshot.hdrEnabled,
            .bitsPerColorChannel = snapshot.bitsPerColorChannel == 0
                                       ? std::nullopt
                                       : std::optional<std::uint16_t>{static_cast<std::uint16_t>(
                                             std::min<std::uint32_t>(snapshot.bitsPerColorChannel, std::numeric_limits<std::uint16_t>::max()))},
            .minimumLuminanceNits = reliableLuminance(snapshot.minimumLuminanceNits),
            .maximumLuminanceNits = reliableLuminance(snapshot.maximumLuminanceNits),
            .maximumFullFrameLuminanceNits = reliableLuminance(snapshot.maximumFullFrameLuminanceNits),
            .sdrWhiteLevelNits = snapshot.sdrWhiteLevelMilli80Nits == 0
                                     ? std::nullopt
                                     : std::optional<float>{static_cast<float>(snapshot.sdrWhiteLevelMilli80Nits) * 80.0F / 1000.0F}};
    }
} // namespace GameWIP::Desktop::Detail::Platform

namespace GameWIP::Desktop::Display
{
    // ------------------------------------------------------------
    // Monitor and display-color queries
    // ------------------------------------------------------------

    Types::Result<std::vector<Types::Display::Info>> getMonitors() noexcept
    {
        return Detail::Platform::getMonitors();
    }

    Types::Result<Types::Display::Info> getPrimaryMonitor() noexcept
    {
        return Detail::Platform::getPrimaryMonitor();
    }

    Types::Result<Types::Display::Info> getMonitor(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getMonitor(monitor);
    }

    Types::Result<Types::Display::ColorInfo> getColorInfo(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getColorInfo(monitor);
    }

    Types::Result<Types::Display::ColorInfo> getColorInfo(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);

        if (state == nullptr || !window.isOpen())
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        }

        if (!window.ownedByCurrentThread())
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        }

        return Detail::Platform::getColorInfo(state->monitor);
    }
} // namespace GameWIP::Desktop::Display
