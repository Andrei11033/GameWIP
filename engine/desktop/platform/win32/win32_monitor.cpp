/// @file win32_monitor.cpp
/// @brief Win32 monitor and display-mode queries for Window.

#include "desktop/platform/win32/internal/win32_display_config.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"
#include "base/checked_arithmetic.h"
#include "base/platform/win32/dynamic_library.h"

#include "desktop/platform/win32/internal/win32_compat.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    using Win32::ActiveDisplayPath;
    using Win32::findActiveDisplayPath;
    using Win32::interlaced;
    using Win32::rationalMillihertz;

    /// ------------------------------------------------------------
    /// Monitor discovery and native metadata
    /// ------------------------------------------------------------

    namespace
    {
        std::mutex monitorRegistryMutex;
        std::unordered_map<std::wstring, Types::Display::MonitorId> monitorIds;
        std::unordered_map<std::uint64_t, std::wstring> monitorDevices;
        std::atomic_uint64_t nextMonitorId{1};

        /// @brief Returns the stable process-local identity for a monitor device name.
        [[nodiscard]] Types::Display::MonitorId idForDevice(std::wstring_view device)
        {
            std::scoped_lock lock(monitorRegistryMutex);
            const auto existing = monitorIds.find(std::wstring(device));
            if(existing != monitorIds.end())
            {
                return existing->second;
            }

            std::uint64_t value = nextMonitorId.fetch_add(1, std::memory_order_relaxed);

            if(value == 0)
            {
                value = nextMonitorId.fetch_add(1, std::memory_order_relaxed);
            }
            std::wstring key(device);
            const Types::Display::MonitorId id{value};
            monitorDevices.emplace(value, key);
            monitorIds.emplace(std::move(key), id);
            return id;
        }

        /// @brief Converts a Win32 DEVMODE into the portable display-mode units.
        [[nodiscard]] Types::Display::Mode toDisplayMode(const DEVMODEW &native) noexcept
        {
            const std::uint32_t frequency = native.dmDisplayFrequency > 1 ? native.dmDisplayFrequency : 0;
            return {
                .resolution = {native.dmPelsWidth, native.dmPelsHeight},
                .refreshRateMillihertz =
                    GameWIP::Base::wouldMultiplyOverflow(frequency, 1000U) ? std::numeric_limits<std::uint32_t>::max() : frequency * 1000U,
                .bitsPerPixel = static_cast<std::uint16_t>(std::min<DWORD>(native.dmBitsPerPel, std::numeric_limits<std::uint16_t>::max())),
                .interlaced = (native.dmDisplayFlags & DM_INTERLACED) != 0};
        }

        /// @brief Queries one current or preferred mode from the monitor device.
        [[nodiscard]] Types::Result<Types::Display::Mode> queryDisplayMode(Types::Display::MonitorId monitor, DWORD selector) noexcept
        {
            if(!monitor.isValid())
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
            }

            try
            {
                const std::wstring device = monitorDeviceName(monitor);

                if(device.empty())
                {
                    return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
                }
                DEVMODEW native{};
                native.dmSize = sizeof(native);

                if(EnumDisplaySettingsExW(device.c_str(), selector, &native, 0) == FALSE)
                {
                    return {.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "EnumDisplaySettingsExW")};
                }
                Types::Display::Mode mode = toDisplayMode(native);

                if(selector == ENUM_CURRENT_SETTINGS)
                {
                    // Prefer DisplayConfig's precise refresh data when available.
                    ActiveDisplayPath active;
                    IO::Types::Status displayPathStatus = findActiveDisplayPath(device, active);

                    if(displayPathStatus.ok())
                    {
                        mode.refreshRateMillihertz = rationalMillihertz(active.path.targetInfo.refreshRate);
                        mode.interlaced = interlaced(active.path.targetInfo.scanLineOrdering);
                    }
                    else if(displayPathStatus.code == IO::Types::ErrorCode::OutOfMemory)
                    {
                        return {.status = std::move(displayPathStatus)};
                    }
                }

                return {.status = IO::successStatus(), .value = mode};
            }
            catch(const std::bad_alloc &)
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
            }
            catch(...)
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
            }
        }

        /// @brief State passed through EnumDisplayMonitors callbacks.
        struct EnumerationContext
        {
            std::vector<Types::Display::Info> *monitors = nullptr;
            IO::Types::Status status;
        };

        /// @brief Appends one enumerated native monitor to the portable result.
        BOOL CALLBACK enumerateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
        {
            auto &context = *reinterpret_cast<EnumerationContext *>(userData);
            Types::Result<Types::Display::Info> result = monitorFromNative(monitor);

            if(!result.status.ok())
            {
                context.status = std::move(result.status);
                return FALSE;
            }

            if(!result.value)
            {
                context.status = IO::makeStatus(IO::Types::ErrorCode::Unknown);
                return FALSE;
            }

            try
            {
                context.monitors->push_back(std::move(*result.value));
                return TRUE;
            }
            catch(const std::bad_alloc &)
            {
                context.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
                return FALSE;
            }
            catch(...)
            {
                context.status = IO::makeStatus(IO::Types::ErrorCode::Unknown);
                return FALSE;
            }
        }

        /// @brief State used to resolve a monitor handle from a device name.
        struct NativeMonitorContext
        {
            std::wstring_view device;
            HMONITOR monitor = nullptr;
        };

        /// @brief Finds the native monitor whose device name matches the requested identity.
        BOOL CALLBACK findNativeMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
        {
            auto &context = *reinterpret_cast<NativeMonitorContext *>(userData);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);

            if(GetMonitorInfoW(monitor, &info) != FALSE && std::wstring_view(info.szDevice) == context.device)
            {
                context.monitor = monitor;
                return FALSE;
            }

            return TRUE;
        }

        /// @brief Converts a capability enum into its bit-mask position.
        [[nodiscard]] constexpr std::uint64_t capabilityBit(Types::Capability capability) noexcept
        {
            return std::uint64_t{1} << static_cast<std::uint8_t>(capability);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Runtime capabilities
    /// ------------------------------------------------------------

    /// @brief Returns the cached Windows build number used for capability gates.
    std::uint32_t runtimeWindowsBuild() noexcept
    {
        static const std::uint32_t build = []
        {
            using RtlGetVersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");

            if(ntdll == nullptr)
            {
                return std::uint32_t{0};
            }
            const auto getVersion = GameWIP::Base::Platform::Win32::loadProcedure<RtlGetVersionFunction>(ntdll, "RtlGetVersion");
            if(getVersion == nullptr)
            {
                return std::uint32_t{0};
            }

            OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);

            if(getVersion(&version) != 0 || version.dwMajorVersion < 10)
            {
                return std::uint32_t{0};
            }
            return static_cast<std::uint32_t>(version.dwBuildNumber);
        }();
        return build;
    }

    /// @brief Tests whether the current Windows build supports system backdrops.
    bool supportsSystemBackdrop() noexcept
    {
        return runtimeWindowsBuild() >= 22621;
    }

    /// @brief Tests whether the current Windows build supports transparent framebuffers.
    bool supportsTransparentFramebuffer() noexcept
    {
        return runtimeWindowsBuild() >= 26100;
    }

    /// @brief Computes all Window capabilities from the current Windows build.
    Types::Result<Types::Capabilities> getCapabilities() noexcept
    {
        using C = Types::Capability;
        std::uint64_t flags = capabilityBit(C::MultipleWindows) | capabilityBit(C::MultipleWindowThreads) | capabilityBit(C::OwnedWindows) |
                              capabilityBit(C::RuntimeOwnerChange) | capabilityBit(C::WindowPositioning) | capabilityBit(C::ProgrammaticFocus) |
                              capabilityBit(C::AttentionRequest) | capabilityBit(C::RuntimeDecorationChange) | capabilityBit(C::CustomChrome) |
                              capabilityBit(C::WindowIcon) | capabilityBit(C::AspectRatioConstraint) | capabilityBit(C::RuntimeInteractionControl) |
                              capabilityBit(C::AlwaysOnTop) | capabilityBit(C::Opacity) | capabilityBit(C::PointerClickThrough) |
                              capabilityBit(C::CursorConfinement) | capabilityBit(C::RelativeCursor) | capabilityBit(C::CursorWarping) |
                              capabilityBit(C::CustomCursor) | capabilityBit(C::FileDrop) | capabilityBit(C::ExclusiveFullscreen) |
                              capabilityBit(C::OcclusionReporting) | capabilityBit(C::ChildSurface) | capabilityBit(C::Accessibility);

        if(supportsSystemBackdrop())
        {
            flags |= capabilityBit(C::SystemBackdrop);
        }

        if(supportsTransparentFramebuffer())
        {
            flags |= capabilityBit(C::TransparentFramebuffer);
        }

        return {
            .status = IO::successStatus(),
            .value = Types::Capabilities{.flags = flags, .maximumCustomChromeRegions = kMaximumChromeRegions, .maximumPointerInputRegions = 0}};
    }

    /// ------------------------------------------------------------
    /// Monitor queries
    /// ------------------------------------------------------------

    /// @brief Converts a native monitor handle into portable geometry and color information.
    Types::Result<Types::Display::Info> monitorFromNative(HMONITOR monitor) noexcept
    {
        if(monitor == nullptr)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::MonitorQuery))
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed)};
        }

        try
        {
            MONITORINFOEXW native{};
            native.cbSize = sizeof(native);

            if(GetMonitorInfoW(monitor, &native) == FALSE)
            {
                return {.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "GetMonitorInfoW")};
            }
            UINT dpiX = kBaselineDpi;
            UINT dpiY = kBaselineDpi;
            const HRESULT dpiResult = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

            if(FAILED(dpiResult))
            {
                dpiX = kBaselineDpi;
                dpiY = kBaselineDpi;
            }

            std::string name;
            DISPLAY_DEVICEW display{};
            display.cb = sizeof(display);

            if(EnumDisplayDevicesW(native.szDevice, 0, &display, 0) != FALSE)
            {
                DWORD nativeCode = ERROR_SUCCESS;

                if(!utf16ToUtf8(display.DeviceString, name, nativeCode))
                {
                    return {
                        .status = statusFromWin32(
                            unicodeConversionError(nativeCode, IO::Types::ErrorCode::EncodingFailed),
                            nativeCode,
                            "monitor name conversion")};
                }
            }

            if(name.empty())
            {
                DWORD nativeCode = ERROR_SUCCESS;

                if(!utf16ToUtf8(native.szDevice, name, nativeCode))
                {
                    return {
                        .status = statusFromWin32(
                            unicodeConversionError(nativeCode, IO::Types::ErrorCode::EncodingFailed),
                            nativeCode,
                            "monitor device conversion")};
                }
            }

            std::uint32_t widthMillimeters = 0;
            std::uint32_t heightMillimeters = 0;
            HDC displayContext = CreateDCW(L"DISPLAY", native.szDevice, nullptr, nullptr);

            if(displayContext != nullptr)
            {
                widthMillimeters = static_cast<std::uint32_t>(std::max(0, GetDeviceCaps(displayContext, HORZSIZE)));
                heightMillimeters = static_cast<std::uint32_t>(std::max(0, GetDeviceCaps(displayContext, VERTSIZE)));
                DeleteDC(displayContext);
            }

            const auto makeRect = [](const RECT &rect) noexcept
            {
                return Types::ScreenRect{
                    .position = {rect.left, rect.top},
                    .size = {
                        static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left)),
                        static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top))}};
            };

            return {
                .status = IO::successStatus(),
                .value = Types::Display::Info{
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
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

    /// @brief Enumerates current native monitors in stable portable form.
    Types::Result<std::vector<Types::Display::Info>> getMonitors() noexcept
    {
        Types::Result<std::vector<Types::Display::Info>> result;

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DisplayEnumeration))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed);
            return result;
        }

        try
        {
            result.value.emplace();
            EnumerationContext context{&*result.value, IO::successStatus()};

            if(EnumDisplayMonitors(nullptr, nullptr, enumerateMonitor, reinterpret_cast<LPARAM>(&context)) == FALSE)
            {
                if(!context.status.ok())
                {
                    result.status = std::move(context.status);
                }
                else
                {
                    result.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "EnumDisplayMonitors");
                }
                result.value.reset();
                return result;
            }

            result.status = IO::successStatus();
            return result;
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

    /// @brief Returns the primary native monitor in portable form.
    Types::Result<Types::Display::Info> getPrimaryMonitor() noexcept
    {
        const POINT origin{};
        return monitorFromNative(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    }

    /// @brief Resolves a stable portable monitor identity to current native information.
    Types::Result<Types::Display::Info> getMonitor(Types::Display::MonitorId monitor) noexcept
    {
        if(!monitor.isValid())
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
        const HMONITOR native = nativeMonitor(monitor);

        if(native == nullptr)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
        }
        return monitorFromNative(native);
    }

    /// @brief Resolves a stable portable monitor identity to an HMONITOR.
    HMONITOR nativeMonitor(Types::Display::MonitorId id) noexcept
    {
        if(!id.isValid())
        {
            return nullptr;
        }

        const std::wstring expected = monitorDeviceName(id);

        if(expected.empty())
        {
            return nullptr;
        }
        NativeMonitorContext context{expected, nullptr};
        static_cast<void>(EnumDisplayMonitors(nullptr, nullptr, findNativeMonitor, reinterpret_cast<LPARAM>(&context)));
        return context.monitor;
    }

    /// @brief Returns the GDI device name associated with a stable monitor identity.
    std::wstring monitorDeviceName(Types::Display::MonitorId id) noexcept
    {
        try
        {
            std::scoped_lock lock(monitorRegistryMutex);
            const auto found = monitorDevices.find(id.value);
            return found == monitorDevices.end() ? std::wstring{} : found->second;
        }
        catch(...)
        {
            return {};
        }
    }

    /// ------------------------------------------------------------
    /// Display modes
    /// ------------------------------------------------------------

    /// @brief Enumerates supported native display modes for one monitor.
    Types::Result<std::vector<Types::Display::Mode>> getModes(Types::Display::MonitorId monitor) noexcept
    {
        if(!monitor.isValid())
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DisplayEnumeration))
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed)};
        }

        try
        {
            const std::wstring device = monitorDeviceName(monitor);

            if(device.empty())
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
            }
            Types::Result<std::vector<Types::Display::Mode>> result;
            result.value.emplace();
            auto &modes = *result.value;

            for(DWORD index = 0;; ++index)
            {
                DEVMODEW native{};
                native.dmSize = sizeof(native);
                SetLastError(ERROR_SUCCESS);

                if(EnumDisplaySettingsExW(device.c_str(), index, &native, EDS_RAWMODE) == FALSE)
                {
                    const DWORD nativeCode = GetLastError();

                    if(nativeCode != ERROR_SUCCESS)
                    {
                        return {.status = statusFromWin32(IO::Types::ErrorCode::StatFailed, nativeCode, "EnumDisplaySettingsExW")};
                    }
                    break;
                }
                const Types::Display::Mode mode = toDisplayMode(native);
                if(mode.resolution.width == 0 || mode.resolution.height == 0)
                {
                    continue;
                }

                if(std::find(modes.begin(), modes.end(), mode) == modes.end())
                {
                    modes.push_back(mode);
                }
            }
            std::sort(
                modes.begin(),
                modes.end(),
                [](const Types::Display::Mode &left, const Types::Display::Mode &right)
                {
                    if(left.resolution.width != right.resolution.width)
                    {
                        return left.resolution.width < right.resolution.width;
                    }

                    if(left.resolution.height != right.resolution.height)
                    {
                        return left.resolution.height < right.resolution.height;
                    }

                    if(left.refreshRateMillihertz != right.refreshRateMillihertz)
                    {
                        return left.refreshRateMillihertz < right.refreshRateMillihertz;
                    }

                    if(left.bitsPerPixel != right.bitsPerPixel)
                    {
                        return left.bitsPerPixel < right.bitsPerPixel;
                    }
                    return left.interlaced < right.interlaced;
                });
            result.status = IO::successStatus();
            return result;
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

    /// @brief Returns the currently active display mode for one monitor.
    Types::Result<Types::Display::Mode> getCurrentMode(Types::Display::MonitorId monitor) noexcept
    {
        return queryDisplayMode(monitor, ENUM_CURRENT_SETTINGS);
    }

    /// @brief Returns the preferred display mode for one monitor.
    Types::Result<Types::Display::Mode> getPreferredMode(Types::Display::MonitorId monitor) noexcept
    {
        if(!monitor.isValid())
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }

        try
        {
            const std::wstring device = monitorDeviceName(monitor);

            if(device.empty())
            {
                return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
            }
            ActiveDisplayPath active;
            IO::Types::Status status = findActiveDisplayPath(device, active);

            if(!status.ok())
            {
                return {.status = std::move(status)};
            }
            DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{
                .header = {
                    .type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,
                    .size = sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE),
                    .adapterId = active.path.targetInfo.adapterId,
                    .id = active.path.targetInfo.id}};
            const LONG nativeResult = DisplayConfigGetDeviceInfo(&preferred.header);

            if(nativeResult != ERROR_SUCCESS)
            {
                return {
                    .status = statusFromWin32(
                        IO::Types::ErrorCode::StatFailed,
                        static_cast<DWORD>(nativeResult),
                        "DisplayConfigGetDeviceInfo preferred mode")};
            }

            const Types::Result<Types::Display::Mode> current = queryDisplayMode(monitor, ENUM_CURRENT_SETTINGS);
            const DISPLAYCONFIG_VIDEO_SIGNAL_INFO &signal = preferred.targetMode.targetVideoSignalInfo;
            return {
                .status = IO::successStatus(),
                .value = Types::Display::Mode{
                    .resolution = {preferred.width, preferred.height},
                    .refreshRateMillihertz = rationalMillihertz(signal.vSyncFreq),
                    .bitsPerPixel = current.ok() ? current->bitsPerPixel : std::uint16_t{0},
                    .interlaced = interlaced(signal.scanLineOrdering)}};
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unknown)};
        }
    }

} // namespace GameWIP::Desktop::Platform
