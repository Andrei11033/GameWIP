/// @file display_info.cpp
/// @brief Rich monitor and display-color inspection for Window.

#include "window/display_info.h"

#include "window/internal/window_platform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GameWIP::Window::Detail::Platform
{
    Types::Display::ColorInfo makeDisplayColorInfo(Types::Display::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept
    {
        const auto reliableLuminance = [](float value) noexcept
        {
            return std::isfinite(value) && value >= 0.0F ? value : 0.0F;
        };

        return {
            .monitor = monitor,
            .activeColorSpace = snapshot.activeColorSpace,
            .wideColorGamutSupported = snapshot.wideColorGamutSupported,
            .hdrSupported = snapshot.hdrSupported,
            .hdrEnabled = snapshot.hdrEnabled,
            .bitsPerColorChannel =
                static_cast<std::uint16_t>(std::min<std::uint32_t>(snapshot.bitsPerColorChannel, std::numeric_limits<std::uint16_t>::max())),
            .minimumLuminanceNits = reliableLuminance(snapshot.minimumLuminanceNits),
            .maximumLuminanceNits = reliableLuminance(snapshot.maximumLuminanceNits),
            .maximumFullFrameLuminanceNits = reliableLuminance(snapshot.maximumFullFrameLuminanceNits),
            .sdrWhiteLevelNits =
                snapshot.sdrWhiteLevelMilli80Nits == 0 ? 0.0F : static_cast<float>(snapshot.sdrWhiteLevelMilli80Nits) * 80.0F / 1000.0F};
    }
} // namespace GameWIP::Window::Detail::Platform

namespace GameWIP::Window::Display
{
    Types::Display::MonitorsResult getMonitors() noexcept
    {
        return Detail::Platform::getMonitors();
    }

    Types::Display::InfoResult getPrimaryMonitor() noexcept
    {
        return Detail::Platform::getPrimaryMonitor();
    }

    Types::Display::InfoResult getMonitor(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getMonitor(monitor);
    }

    Types::Display::ColorInfoResult getColorInfo(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getColorInfo(monitor);
    }

    Types::Display::ColorInfoResult getColorInfo(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !window.isOpen())
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        if (!window.isOwnedByCurrentThread())
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        return Detail::Platform::getColorInfo(state->monitor);
    }
} // namespace GameWIP::Window::Display
