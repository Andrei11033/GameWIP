#pragma once

namespace GameWIP
{
    // Monitor info

    WindowResult Window::getMonitors(std::vector<MonitorInfo> &outMonitors)
    {
        outMonitors.clear();

        if (nativeWindow == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (!nativeWindow->monitorCache.empty())
        {
            outMonitors = nativeWindow->monitorCache;
            return recordResult(WindowResult::Success);
        }

        std::vector<MonitorInfo> monitors;
        MonitorEnumerationContext context{.monitors = &monitors};
        if (EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&context)) == FALSE)
        {
            return recordResult(WindowResult::PlatformCallFailed, context.error);
        }

        nativeWindow->monitorCache = monitors;
        outMonitors = nativeWindow->monitorCache;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getCurrentMonitor(MonitorInfo &outMonitor)
    {
        outMonitor = {};

        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        HMONITOR hMonitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);
        if (hMonitor == nullptr)
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        unsigned long error = 0;
        if (!buildMonitorInfo(hMonitor, outMonitor, error))
        {
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getDisplayModes(const MonitorInfo &monitor, std::vector<DisplayMode> &outModes)
    {
        outModes.clear();

        if (nativeWindow == nullptr || monitor.handle == nullptr || monitor.deviceName.empty())
        {
            return recordResult(nativeWindow == nullptr ? WindowResult::NotCreated : WindowResult::InvalidMonitor);
        }

        auto cacheIt = nativeWindow->displayModeCache.find(monitor.deviceName);
        if (cacheIt != nativeWindow->displayModeCache.end())
        {
            outModes = cacheIt->second;
            return recordResult(WindowResult::Success);
        }

        std::vector<DisplayMode> modes;

        nativeWindow->utf16Scratch.clear();
        if (!utf8ToWide(monitor.deviceName, nativeWindow->utf16Scratch))
        {
            return recordResult(WindowResult::InvalidMonitor, GetLastError());
        }

        DWORD modeNum = 0;
        for (;;)
        {
            DEVMODEW devMode{};
            devMode.dmSize = sizeof(devMode);
            SetLastError(0);
            if (EnumDisplaySettingsW(nativeWindow->utf16Scratch.c_str(), modeNum, &devMode) == FALSE)
            {
                if (modeNum == 0)
                {
                    return recordResult(WindowResult::InvalidMonitor, GetLastError());
                }

                break;
            }

            DisplayMode mode{
                static_cast<int>(devMode.dmPelsWidth),
                static_cast<int>(devMode.dmPelsHeight),
                static_cast<int>(devMode.dmDisplayFrequency),
                static_cast<int>(devMode.dmBitsPerPel)};

            if (!containsDisplayMode(modes, mode))
            {
                modes.push_back(mode);
            }

            modeNum++;
        }

        nativeWindow->displayModeCache.emplace(monitor.deviceName, std::move(modes));
        outModes = nativeWindow->displayModeCache.find(monitor.deviceName)->second;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getCurrentDisplayMode(DisplayMode &outMode)
    {
        outMode = {};

        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        MonitorInfo monitor{};
        WindowResult monitorResult = getCurrentMonitor(monitor);
        if (monitorResult != WindowResult::Success)
        {
            return monitorResult;
        }

        if (monitor.deviceName.empty())
        {
            return recordResult(WindowResult::InvalidMonitor);
        }

        nativeWindow->utf16Scratch.clear();
        if (!utf8ToWide(monitor.deviceName, nativeWindow->utf16Scratch))
        {
            return recordResult(WindowResult::InvalidMonitor, GetLastError());
        }

        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsW(nativeWindow->utf16Scratch.c_str(), ENUM_CURRENT_SETTINGS, &devMode) == FALSE)
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        outMode = DisplayMode{
            static_cast<int>(devMode.dmPelsWidth),
            static_cast<int>(devMode.dmPelsHeight),
            static_cast<int>(devMode.dmDisplayFrequency),
            static_cast<int>(devMode.dmBitsPerPel)};
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::setFullscreenDisplayMode(const MonitorInfo &monitor, const DisplayMode &mode)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (nativeWindow->role != WindowRole::MainGame)
        {
            return recordResult(WindowResult::OperationNotAllowed);
        }

        if (monitor.handle == nullptr || monitor.deviceName.empty())
        {
            return recordResult(WindowResult::InvalidMonitor);
        }

        if (!isCompleteDisplayMode(mode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        std::vector<DisplayMode> modes;
        WindowResult modesResult = getDisplayModes(monitor, modes);
        if (modesResult != WindowResult::Success)
        {
            return modesResult;
        }

        if (!containsDisplayMode(modes, mode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        MonitorInfo previousMonitor = nativeWindow->requestedFullscreenMonitor;
        DisplayMode previousMode = nativeWindow->requestedFullscreenDisplayMode;
        bool hadPreviousMode = nativeWindow->hasRequestedFullscreenDisplayMode;

        nativeWindow->requestedFullscreenMonitor = monitor;
        nativeWindow->requestedFullscreenDisplayMode = mode;
        nativeWindow->hasRequestedFullscreenDisplayMode = true;

        if (nativeWindow->mode == WindowMode::Fullscreen)
        {
            WindowResult applyResult = applyFullscreenMode();
            if (applyResult != WindowResult::Success)
            {
                nativeWindow->requestedFullscreenMonitor = previousMonitor;
                nativeWindow->requestedFullscreenDisplayMode = previousMode;
                nativeWindow->hasRequestedFullscreenDisplayMode = hadPreviousMode;
                return applyResult;
            }
        }

        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getFullscreenDisplayMode(MonitorInfo &outMonitor, DisplayMode &outMode)
    {
        outMonitor = {};
        outMode = {};

        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (nativeWindow->hasRequestedFullscreenDisplayMode)
        {
            outMonitor = nativeWindow->requestedFullscreenMonitor;
            outMode = nativeWindow->requestedFullscreenDisplayMode;
            return recordResult(WindowResult::Success);
        }

        if (nativeWindow->mode == WindowMode::Fullscreen && nativeWindow->activeFullscreenMonitor.handle != nullptr &&
            isCompleteDisplayMode(nativeWindow->activeFullscreenDisplayMode))
        {
            outMonitor = nativeWindow->activeFullscreenMonitor;
            outMode = nativeWindow->activeFullscreenDisplayMode;
            return recordResult(WindowResult::Success);
        }

        WindowResult monitorResult = getCurrentMonitor(outMonitor);
        if (monitorResult != WindowResult::Success)
        {
            return monitorResult;
        }

        std::vector<DisplayMode> modes;
        WindowResult modesResult = getDisplayModes(outMonitor, modes);
        if (modesResult != WindowResult::Success)
        {
            return modesResult;
        }

        if (!chooseHighestRefreshDisplayMode(modes, nativeWindow->requestedClientWidth, nativeWindow->requestedClientHeight, outMode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        return recordResult(WindowResult::Success);
    }
} // namespace GameWIP
