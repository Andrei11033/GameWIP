/// @file win32_monitor.cpp
/// @brief Win32 monitor, display-mode, and capability implementation for Window.

#include "window/platform/win32/internal/win32_window_backend.h"

#include <algorithm>
#include <cwchar>
#include <limits>
#include <new>
#include <unordered_map>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        std::mutex monitorRegistryMutex;
        std::unordered_map<std::wstring, Types::MonitorId> monitorIds;
        std::unordered_map<std::uint64_t, std::wstring> monitorDevices;
        std::atomic_uint64_t nextMonitorId{1};

        [[nodiscard]] Types::MonitorId idForDevice(std::wstring_view device)
        {
            std::scoped_lock lock(monitorRegistryMutex);
            const auto existing = monitorIds.find(std::wstring(device));
            if (existing != monitorIds.end())
                return existing->second;

            std::uint64_t value = nextMonitorId.fetch_add(1, std::memory_order_relaxed);
            if (value == 0)
                value = nextMonitorId.fetch_add(1, std::memory_order_relaxed);
            std::wstring key(device);
            const Types::MonitorId id{value};
            monitorDevices.emplace(value, key);
            monitorIds.emplace(std::move(key), id);
            return id;
        }

        [[nodiscard]] Types::DisplayMode toDisplayMode(const DEVMODEW &native) noexcept
        {
            const std::uint32_t frequency = native.dmDisplayFrequency > 1 ? native.dmDisplayFrequency : 0;
            return {
                .resolution = {native.dmPelsWidth, native.dmPelsHeight},
                .refreshRateMillihertz =
                    frequency > std::numeric_limits<std::uint32_t>::max() / 1000U ? std::numeric_limits<std::uint32_t>::max() : frequency * 1000U,
                .bitsPerPixel = static_cast<std::uint16_t>(std::min<DWORD>(native.dmBitsPerPel, std::numeric_limits<std::uint16_t>::max())),
                .interlaced = (native.dmDisplayFlags & DM_INTERLACED) != 0};
        }

        [[nodiscard]] Types::DisplayModeResult queryDisplayMode(Types::MonitorId monitor, DWORD selector) noexcept
        {
            if (!monitor.valid())
                return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
            try
            {
                const std::wstring device = monitorDeviceName(monitor);
                if (device.empty())
                    return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
                DEVMODEW native{};
                native.dmSize = sizeof(native);
                if (EnumDisplaySettingsExW(device.c_str(), selector, &native, 0) == FALSE)
                    return {.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "EnumDisplaySettingsExW")};
                return {.status = IO::successStatus(), .displayMode = toDisplayMode(native)};
            }
            catch (const std::bad_alloc &)
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
            }
            catch (...)
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
            }
        }

        struct EnumerationContext
        {
            std::vector<Types::MonitorInfo> *monitors = nullptr;
            IO::Types::Status status;
        };

        BOOL CALLBACK enumerateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
        {
            auto &context = *reinterpret_cast<EnumerationContext *>(userData);
            Types::MonitorInfoResult result = monitorFromNative(monitor);
            if (!result.status.ok())
            {
                context.status = std::move(result.status);
                return FALSE;
            }
            try
            {
                context.monitors->push_back(std::move(result.monitor));
                return TRUE;
            }
            catch (const std::bad_alloc &)
            {
                context.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
                return FALSE;
            }
            catch (...)
            {
                context.status = IO::makeStatus(IO::Types::ErrorCode::Unknown);
                return FALSE;
            }
        }

        struct NativeMonitorContext
        {
            const wchar_t *device = nullptr;
            HMONITOR monitor = nullptr;
        };

        BOOL CALLBACK findNativeMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
        {
            auto &context = *reinterpret_cast<NativeMonitorContext *>(userData);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) != FALSE && std::wcscmp(info.szDevice, context.device) == 0)
            {
                context.monitor = monitor;
                return FALSE;
            }
            return TRUE;
        }

        [[nodiscard]] constexpr std::uint64_t capabilityBit(Types::Capability capability) noexcept
        {
            return std::uint64_t{1} << static_cast<std::uint8_t>(capability);
        }
    } // namespace

    Types::CapabilitiesResult getCapabilities() noexcept
    {
        using C = Types::Capability;
        constexpr std::uint64_t flags =
            capabilityBit(C::MultipleWindows) | capabilityBit(C::MultipleWindowThreads) | capabilityBit(C::OwnedWindows) |
            capabilityBit(C::RuntimeOwnerChange) | capabilityBit(C::WindowPositioning) | capabilityBit(C::ProgrammaticFocus) |
            capabilityBit(C::AttentionRequest) | capabilityBit(C::RuntimeDecorationChange) | capabilityBit(C::CustomChrome) |
            capabilityBit(C::WindowIcon) | capabilityBit(C::AspectRatioConstraint) | capabilityBit(C::RuntimeInteractionControl) |
            capabilityBit(C::AlwaysOnTop) | capabilityBit(C::Opacity) | capabilityBit(C::TransparentFramebuffer) | capabilityBit(C::BackdropBlur) |
            capabilityBit(C::PointerClickThrough) | capabilityBit(C::PointerRegions) | capabilityBit(C::CursorConfinement) |
            capabilityBit(C::RelativeCursor) | capabilityBit(C::CursorWarping) | capabilityBit(C::FileDrop) | capabilityBit(C::ExclusiveFullscreen);
        return {
            .status = IO::successStatus(),
            .capabilities = {
                .flags = flags,
                .maximumCustomChromeRegions = kMaximumChromeRegions,
                .maximumPointerInputRegions = kMaximumPointerRegions}};
    }

    Types::MonitorInfoResult monitorFromNative(HMONITOR monitor) noexcept
    {
        if (monitor == nullptr)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        if (Detail::consumeFailure(TestHooks::FailurePoint::MonitorQuery))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed)};
        try
        {
            MONITORINFOEXW native{};
            native.cbSize = sizeof(native);
            if (GetMonitorInfoW(monitor, &native) == FALSE)
                return {.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "GetMonitorInfoW")};

            UINT dpiX = kBaselineDpi;
            UINT dpiY = kBaselineDpi;
            const HRESULT dpiResult = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            if (FAILED(dpiResult))
            {
                dpiX = kBaselineDpi;
                dpiY = kBaselineDpi;
            }

            std::string name;
            DISPLAY_DEVICEW display{};
            display.cb = sizeof(display);
            if (EnumDisplayDevicesW(native.szDevice, 0, &display, 0) != FALSE)
            {
                DWORD nativeCode = ERROR_SUCCESS;
                if (!utf16ToUtf8(display.DeviceString, name, nativeCode))
                    return {.status = statusFromWin32(IO::Types::ErrorCode::EncodingFailed, nativeCode, "monitor name conversion")};
            }
            if (name.empty())
            {
                DWORD nativeCode = ERROR_SUCCESS;
                if (!utf16ToUtf8(native.szDevice, name, nativeCode))
                    return {.status = statusFromWin32(IO::Types::ErrorCode::EncodingFailed, nativeCode, "monitor device conversion")};
            }

            std::uint32_t widthMillimeters = 0;
            std::uint32_t heightMillimeters = 0;
            HDC displayContext = CreateDCW(L"DISPLAY", native.szDevice, nullptr, nullptr);
            if (displayContext != nullptr)
            {
                widthMillimeters = static_cast<std::uint32_t>(std::max(0, GetDeviceCaps(displayContext, HORZSIZE)));
                heightMillimeters = static_cast<std::uint32_t>(std::max(0, GetDeviceCaps(displayContext, VERTSIZE)));
                DeleteDC(displayContext);
            }

            const auto makeRect = [dpiX, dpiY](const RECT &rect) noexcept
            {
                const std::int32_t left = MulDiv(rect.left, kBaselineDpi, static_cast<int>(dpiX));
                const std::int32_t top = MulDiv(rect.top, kBaselineDpi, static_cast<int>(dpiY));
                const std::int32_t right = MulDiv(rect.right, kBaselineDpi, static_cast<int>(dpiX));
                const std::int32_t bottom = MulDiv(rect.bottom, kBaselineDpi, static_cast<int>(dpiY));
                return Types::Rect{
                    .position = {left, top},
                    .size = {static_cast<std::uint32_t>(std::max(0, right - left)), static_cast<std::uint32_t>(std::max(0, bottom - top))}};
            };

            return {
                .status = IO::successStatus(),
                .monitor = {
                    .id = idForDevice(native.szDevice),
                    .name = std::move(name),
                    .bounds = makeRect(native.rcMonitor),
                    .workArea = makeRect(native.rcWork),
                    .contentScale =
                        {static_cast<float>(dpiX) / static_cast<float>(kBaselineDpi), static_cast<float>(dpiY) / static_cast<float>(kBaselineDpi)},
                    .effectiveDpi = {static_cast<float>(dpiX), static_cast<float>(dpiY)},
                    .physicalWidthMillimeters = widthMillimeters,
                    .physicalHeightMillimeters = heightMillimeters,
                    .primary = (native.dwFlags & MONITORINFOF_PRIMARY) != 0}};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

    Types::MonitorListResult getMonitors() noexcept
    {
        Types::MonitorListResult result;
        if (Detail::consumeFailure(TestHooks::FailurePoint::DisplayEnumeration))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed);
            return result;
        }
        try
        {
            EnumerationContext context{&result.monitors, IO::successStatus()};
            if (EnumDisplayMonitors(nullptr, nullptr, enumerateMonitor, reinterpret_cast<LPARAM>(&context)) == FALSE)
            {
                if (!context.status.ok())
                    result.status = std::move(context.status);
                else
                    result.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "EnumDisplayMonitors");
                result.monitors.clear();
                return result;
            }
            result.status = IO::successStatus();
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

    Types::MonitorInfoResult getPrimaryMonitor() noexcept
    {
        const POINT origin{};
        return monitorFromNative(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    }

    Types::MonitorInfoResult getMonitor(Types::MonitorId monitor) noexcept
    {
        if (!monitor.valid())
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        const HMONITOR native = nativeMonitor(monitor);
        if (native == nullptr)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
        return monitorFromNative(native);
    }

    HMONITOR nativeMonitor(Types::MonitorId id) noexcept
    {
        if (!id.valid())
            return nullptr;
        const std::wstring expected = monitorDeviceName(id);
        if (expected.empty())
            return nullptr;
        NativeMonitorContext context{expected.c_str(), nullptr};
        static_cast<void>(EnumDisplayMonitors(nullptr, nullptr, findNativeMonitor, reinterpret_cast<LPARAM>(&context)));
        return context.monitor;
    }

    std::wstring monitorDeviceName(Types::MonitorId id) noexcept
    {
        try
        {
            std::scoped_lock lock(monitorRegistryMutex);
            const auto found = monitorDevices.find(id.value);
            return found == monitorDevices.end() ? std::wstring{} : found->second;
        }
        catch (...)
        {
            return {};
        }
    }

    Types::DisplayModeListResult getDisplayModes(Types::MonitorId monitor) noexcept
    {
        if (!monitor.valid())
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        if (Detail::consumeFailure(TestHooks::FailurePoint::DisplayEnumeration))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed)};
        try
        {
            const std::wstring device = monitorDeviceName(monitor);
            if (device.empty())
                return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
            Types::DisplayModeListResult result;
            for (DWORD index = 0;; ++index)
            {
                DEVMODEW native{};
                native.dmSize = sizeof(native);
                SetLastError(ERROR_SUCCESS);
                if (EnumDisplaySettingsExW(device.c_str(), index, &native, EDS_RAWMODE) == FALSE)
                {
                    const DWORD nativeCode = GetLastError();
                    if (nativeCode != ERROR_SUCCESS)
                        return {.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, nativeCode, "EnumDisplaySettingsExW")};
                    break;
                }
                const Types::DisplayMode mode = toDisplayMode(native);
                if (mode.resolution.width == 0 || mode.resolution.height == 0)
                    continue;
                if (std::find(result.displayModes.begin(), result.displayModes.end(), mode) == result.displayModes.end())
                    result.displayModes.push_back(mode);
            }
            result.status = IO::successStatus();
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

    Types::DisplayModeResult getCurrentDisplayMode(Types::MonitorId monitor) noexcept
    {
        return queryDisplayMode(monitor, ENUM_CURRENT_SETTINGS);
    }

    Types::DisplayModeResult getPreferredDisplayMode(Types::MonitorId monitor) noexcept
    {
        return queryDisplayMode(monitor, ENUM_REGISTRY_SETTINGS);
    }
} // namespace GameWIP::Window::Detail::Platform
