/// @file win32_monitor.cpp
/// @brief Win32 monitor, display-mode, and capability implementation for Window.

#include "window/platform/win32/internal/win32_window_backend.h"

#include "window/platform/win32/internal/win32_compat.h"

#include <dxgi1_6.h>

#include <algorithm>
#include <cwchar>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        std::mutex monitorRegistryMutex;
        std::unordered_map<std::wstring, Types::MonitorId> monitorIds;
        std::unordered_map<std::uint64_t, std::wstring> monitorDevices;
        std::atomic_uint64_t nextMonitorId{1};

        template <typename Interface> class ComReference final
        {
        public:
            ComReference() noexcept = default;
            ~ComReference() noexcept
            {
                reset();
            }

            ComReference(const ComReference &) = delete;
            ComReference &operator=(const ComReference &) = delete;

            [[nodiscard]] Interface *get() const noexcept
            {
                return value;
            }

            [[nodiscard]] Interface **put() noexcept
            {
                reset();
                return &value;
            }

            void reset() noexcept
            {
                if (value != nullptr)
                {
                    value->Release();
                    value = nullptr;
                }
            }

            [[nodiscard]] Interface *operator->() const noexcept
            {
                return value;
            }

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return value != nullptr;
            }

        private:
            Interface *value = nullptr;
        };

        struct DisplayColorFactoryState
        {
            ComReference<IDXGIFactory1> factory;
            bool queried = false;
#if INTERNAL_WINDOW_TEST_HOOKS
            bool forceConfigurationChange = false;
            bool forceMetadataUnavailable = false;
#endif
        };

        thread_local DisplayColorFactoryState displayColorFactory;

        [[nodiscard]] bool ensureDisplayColorFactory() noexcept
        {
            if (displayColorFactory.factory && displayColorFactory.factory->IsCurrent() != FALSE)
                return true;

            displayColorFactory.factory.reset();
            return SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void **>(displayColorFactory.factory.put())));
        }

        void addDxgiColorMetadata(HMONITOR monitor, DisplayColorSnapshot &snapshot) noexcept
        {
            if (!ensureDisplayColorFactory())
                return;

            for (UINT adapterIndex = 0;; ++adapterIndex)
            {
                ComReference<IDXGIAdapter1> adapter;
                const HRESULT adapterResult = displayColorFactory.factory->EnumAdapters1(adapterIndex, adapter.put());
                if (adapterResult == DXGI_ERROR_NOT_FOUND)
                    return;
                if (FAILED(adapterResult))
                    return;

                for (UINT outputIndex = 0;; ++outputIndex)
                {
                    ComReference<IDXGIOutput> output;
                    const HRESULT outputResult = adapter->EnumOutputs(outputIndex, output.put());
                    if (outputResult == DXGI_ERROR_NOT_FOUND)
                        break;
                    if (FAILED(outputResult))
                        break;

                    DXGI_OUTPUT_DESC outputDescription{};
                    if (FAILED(output->GetDesc(&outputDescription)) || outputDescription.Monitor != monitor)
                        continue;

                    ComReference<IDXGIOutput6> output6;
                    if (FAILED(output->QueryInterface(IID_IDXGIOutput6, reinterpret_cast<void **>(output6.put()))))
                        return;

                    DXGI_OUTPUT_DESC1 colorDescription{};
                    if (FAILED(output6->GetDesc1(&colorDescription)))
                        return;

                    if (snapshot.bitsPerColorChannel == 0)
                        snapshot.bitsPerColorChannel = colorDescription.BitsPerColor;
                    snapshot.minimumLuminanceNits = colorDescription.MinLuminance;
                    snapshot.maximumLuminanceNits = colorDescription.MaxLuminance;
                    snapshot.maximumFullFrameLuminanceNits = colorDescription.MaxFullFrameLuminance;

                    if (colorDescription.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                    {
                        snapshot.activeColorSpace = Types::DisplayColorSpace::Hdr10Pq;
                        snapshot.wideColorGamutSupported = true;
                        snapshot.hdrSupported = true;
                        snapshot.hdrEnabled = true;
                    }
                    else if (
                        colorDescription.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
                        snapshot.activeColorSpace == Types::DisplayColorSpace::Unknown)
                    {
                        snapshot.activeColorSpace = Types::DisplayColorSpace::Srgb;
                    }
                    return;
                }
            }
        }

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

        [[nodiscard]] std::uint32_t rationalMillihertz(DISPLAYCONFIG_RATIONAL value) noexcept
        {
            if (value.Numerator == 0 || value.Denominator == 0)
                return 0;
            const std::uint64_t scaled = static_cast<std::uint64_t>(value.Numerator) * 1000U;
            const std::uint64_t rounded = (scaled + value.Denominator / 2U) / value.Denominator;
            return static_cast<std::uint32_t>(std::min<std::uint64_t>(rounded, std::numeric_limits<std::uint32_t>::max()));
        }

        [[nodiscard]] bool interlaced(DISPLAYCONFIG_SCANLINE_ORDERING ordering) noexcept
        {
            return ordering == DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED || ordering == DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_LOWERFIELDFIRST;
        }

        struct ActiveDisplayPath
        {
            DISPLAYCONFIG_PATH_INFO path;
        };

        [[nodiscard]] IO::Types::Status findActiveDisplayPath(std::wstring_view device, ActiveDisplayPath &result) noexcept
        {
            constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;
            for (unsigned int attempt = 0; attempt < 4; ++attempt)
            {
                UINT32 pathCount = 0;
                UINT32 modeCount = 0;
                LONG nativeResult = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);
                if (nativeResult != ERROR_SUCCESS)
                    return statusFromWin32(IO::Types::ErrorCode::StatFailed, static_cast<DWORD>(nativeResult), "GetDisplayConfigBufferSizes");

                std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
                std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
                nativeResult = QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
                if (nativeResult == ERROR_INSUFFICIENT_BUFFER)
                    continue;
                if (nativeResult != ERROR_SUCCESS)
                    return statusFromWin32(IO::Types::ErrorCode::StatFailed, static_cast<DWORD>(nativeResult), "QueryDisplayConfig");

                paths.resize(pathCount);
                for (const DISPLAYCONFIG_PATH_INFO &path : paths)
                {
                    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{
                        .header = {
                            .type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                            .size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME),
                            .adapterId = path.sourceInfo.adapterId,
                            .id = path.sourceInfo.id}};
                    nativeResult = DisplayConfigGetDeviceInfo(&source.header);
                    if (nativeResult != ERROR_SUCCESS)
                        continue;
                    if (_wcsicmp(source.viewGdiDeviceName, std::wstring(device).c_str()) == 0)
                    {
                        result.path = path;
                        return IO::successStatus();
                    }
                }
                return IO::makeStatus(IO::Types::ErrorCode::NotFound);
            }
            return IO::makeStatus(
                IO::Types::ErrorCode::ResourceBusy,
                ERROR_INSUFFICIENT_BUFFER,
                "display topology changed repeatedly during QueryDisplayConfig");
        }

        void addDisplayConfigColorMetadata(const ActiveDisplayPath &active, DisplayColorSnapshot &snapshot) noexcept
        {
            Compat::AdvancedColorInfo2 advanced{
                .header = {
                    .type = Compat::kGetAdvancedColorInfo2,
                    .size = sizeof(Compat::AdvancedColorInfo2),
                    .adapterId = active.path.targetInfo.adapterId,
                    .id = active.path.targetInfo.id}};
            if (DisplayConfigGetDeviceInfo(&advanced.header) == ERROR_SUCCESS)
            {
                snapshot.hdrSupported = (advanced.flags & Compat::kHighDynamicRangeSupported) != 0;
                snapshot.hdrEnabled =
                    (advanced.flags & Compat::kHighDynamicRangeUserEnabled) != 0 || advanced.activeColorMode == Compat::AdvancedColorMode::Hdr;
                snapshot.wideColorGamutSupported = (advanced.flags & (Compat::kWideColorSupported | Compat::kHighDynamicRangeSupported)) != 0;
                snapshot.bitsPerColorChannel = advanced.bitsPerColorChannel;

                switch (advanced.activeColorMode)
                {
                case Compat::AdvancedColorMode::Sdr:
                    snapshot.activeColorSpace = Types::DisplayColorSpace::Srgb;
                    break;
                case Compat::AdvancedColorMode::WideColorGamut:
                    snapshot.activeColorSpace = Types::DisplayColorSpace::WideColorGamut;
                    break;
                case Compat::AdvancedColorMode::Hdr:
                    snapshot.activeColorSpace = Types::DisplayColorSpace::Hdr10Pq;
                    break;
                default:
                    if ((advanced.flags & Compat::kAdvancedColorActive) == 0)
                        snapshot.activeColorSpace = Types::DisplayColorSpace::Srgb;
                    break;
                }

                if ((advanced.flags & Compat::kWideColorUserEnabled) != 0 && snapshot.activeColorSpace == Types::DisplayColorSpace::Unknown)
                    snapshot.activeColorSpace = Types::DisplayColorSpace::WideColorGamut;
            }
            else
            {
                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy{
                    .header = {
                        .type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,
                        .size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO),
                        .adapterId = active.path.targetInfo.adapterId,
                        .id = active.path.targetInfo.id}};
                if (DisplayConfigGetDeviceInfo(&legacy.header) == ERROR_SUCCESS)
                {
                    snapshot.wideColorGamutSupported = legacy.advancedColorSupported != 0;
                    snapshot.hdrSupported = legacy.advancedColorSupported != 0;
                    snapshot.hdrEnabled = legacy.advancedColorEnabled != 0;
                    snapshot.bitsPerColorChannel = legacy.bitsPerColorChannel;
                    snapshot.activeColorSpace = legacy.advancedColorEnabled != 0 ? Types::DisplayColorSpace::Unknown : Types::DisplayColorSpace::Srgb;
                }
            }

            DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel{
                .header = {
                    .type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
                    .size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL),
                    .adapterId = active.path.targetInfo.adapterId,
                    .id = active.path.targetInfo.id}};
            if (DisplayConfigGetDeviceInfo(&whiteLevel.header) == ERROR_SUCCESS)
                snapshot.sdrWhiteLevelMilli80Nits = whiteLevel.SDRWhiteLevel;
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
                Types::DisplayMode mode = toDisplayMode(native);
                if (selector == ENUM_CURRENT_SETTINGS)
                {
                    ActiveDisplayPath active;
                    if (findActiveDisplayPath(device, active).ok())
                    {
                        mode.refreshRateMillihertz = rationalMillihertz(active.path.targetInfo.refreshRate);
                        mode.interlaced = interlaced(active.path.targetInfo.scanLineOrdering);
                    }
                }
                return {.status = IO::successStatus(), .displayMode = mode};
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

    std::uint32_t runtimeWindowsBuild() noexcept
    {
        static const std::uint32_t build = []
        {
            using RtlGetVersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (ntdll == nullptr)
                return std::uint32_t{0};
            const auto getVersion = reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (getVersion == nullptr)
                return std::uint32_t{0};
            OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);
            if (getVersion(&version) != 0 || version.dwMajorVersion < 10)
                return std::uint32_t{0};
            return static_cast<std::uint32_t>(version.dwBuildNumber);
        }();
        return build;
    }

    bool supportsSystemBackdrop() noexcept
    {
        return runtimeWindowsBuild() >= 22621;
    }

    bool supportsTransparentFramebuffer() noexcept
    {
        return runtimeWindowsBuild() >= 26100;
    }

    Types::CapabilitiesResult getCapabilities() noexcept
    {
        using C = Types::Capability;
        std::uint64_t flags = capabilityBit(C::MultipleWindows) | capabilityBit(C::MultipleWindowThreads) | capabilityBit(C::OwnedWindows) |
                              capabilityBit(C::RuntimeOwnerChange) | capabilityBit(C::WindowPositioning) | capabilityBit(C::ProgrammaticFocus) |
                              capabilityBit(C::AttentionRequest) | capabilityBit(C::RuntimeDecorationChange) | capabilityBit(C::CustomChrome) |
                              capabilityBit(C::WindowIcon) | capabilityBit(C::AspectRatioConstraint) | capabilityBit(C::RuntimeInteractionControl) |
                              capabilityBit(C::AlwaysOnTop) | capabilityBit(C::Opacity) | capabilityBit(C::PointerClickThrough) |
                              capabilityBit(C::CursorConfinement) | capabilityBit(C::RelativeCursor) | capabilityBit(C::CursorWarping) |
                              capabilityBit(C::FileDrop) | capabilityBit(C::ExclusiveFullscreen);
        if (supportsSystemBackdrop())
            flags |= capabilityBit(C::SystemBackdrop);
        if (supportsTransparentFramebuffer())
            flags |= capabilityBit(C::TransparentFramebuffer);
        return {
            .status = IO::successStatus(),
            .capabilities = {.flags = flags, .maximumCustomChromeRegions = kMaximumChromeRegions, .maximumPointerInputRegions = 0}};
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
            std::sort(
                result.displayModes.begin(),
                result.displayModes.end(),
                [](const Types::DisplayMode &left, const Types::DisplayMode &right)
                {
                    if (left.resolution.width != right.resolution.width)
                        return left.resolution.width < right.resolution.width;
                    if (left.resolution.height != right.resolution.height)
                        return left.resolution.height < right.resolution.height;
                    if (left.refreshRateMillihertz != right.refreshRateMillihertz)
                        return left.refreshRateMillihertz < right.refreshRateMillihertz;
                    if (left.bitsPerPixel != right.bitsPerPixel)
                        return left.bitsPerPixel < right.bitsPerPixel;
                    return left.interlaced < right.interlaced;
                });
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
        if (!monitor.valid())
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        try
        {
            const std::wstring device = monitorDeviceName(monitor);
            if (device.empty())
                return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};

            ActiveDisplayPath active;
            IO::Types::Status status = findActiveDisplayPath(device, active);
            if (!status.ok())
                return {.status = std::move(status)};

            DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{
                .header = {
                    .type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,
                    .size = sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE),
                    .adapterId = active.path.targetInfo.adapterId,
                    .id = active.path.targetInfo.id}};
            const LONG nativeResult = DisplayConfigGetDeviceInfo(&preferred.header);
            if (nativeResult != ERROR_SUCCESS)
            {
                return {
                    .status = statusFromWin32(
                        IO::Types::ErrorCode::StatFailed,
                        static_cast<DWORD>(nativeResult),
                        "DisplayConfigGetDeviceInfo preferred mode")};
            }

            const Types::DisplayModeResult current = queryDisplayMode(monitor, ENUM_CURRENT_SETTINGS);
            const DISPLAYCONFIG_VIDEO_SIGNAL_INFO &signal = preferred.targetMode.targetVideoSignalInfo;
            return {
                .status = IO::successStatus(),
                .displayMode = {
                    .resolution = {preferred.width, preferred.height},
                    .refreshRateMillihertz = rationalMillihertz(signal.vSyncFreq),
                    .bitsPerPixel = current.status.ok() ? current.displayMode.bitsPerPixel : std::uint16_t{0},
                    .interlaced = interlaced(signal.scanLineOrdering)}};
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

    Types::DisplayColorInfoResult getDisplayColorInfo(Types::MonitorId monitor) noexcept
    {
        if (!monitor.valid())
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        if (Detail::consumeFailure(TestHooks::FailurePoint::DisplayColorQuery))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::StatFailed)};
        try
        {
            const std::wstring device = monitorDeviceName(monitor);
            if (device.empty())
                return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};
            const HMONITOR native = nativeMonitor(monitor);
            if (native == nullptr)
                return {.status = IO::makeStatus(IO::Types::ErrorCode::NotFound)};

            ActiveDisplayPath active;
            IO::Types::Status status = findActiveDisplayPath(device, active);
            if (!status.ok())
                return {.status = std::move(status)};

            displayColorFactory.queried = true;
            DisplayColorSnapshot snapshot;
#if INTERNAL_WINDOW_TEST_HOOKS
            if (displayColorFactory.forceMetadataUnavailable)
            {
                displayColorFactory.forceMetadataUnavailable = false;
                return {.status = IO::successStatus(), .info = makeDisplayColorInfo(monitor, snapshot)};
            }
#endif
            addDisplayConfigColorMetadata(active, snapshot);
            addDxgiColorMetadata(native, snapshot);
            return {.status = IO::successStatus(), .info = makeDisplayColorInfo(monitor, snapshot)};
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

    bool consumeDisplayColorConfigurationChange() noexcept
    {
#if INTERNAL_WINDOW_TEST_HOOKS
        if (displayColorFactory.forceConfigurationChange)
        {
            displayColorFactory.forceConfigurationChange = false;
            displayColorFactory.factory.reset();
            return true;
        }
#endif
        if (!displayColorFactory.queried || !displayColorFactory.factory || displayColorFactory.factory->IsCurrent() != FALSE)
            return false;
        displayColorFactory.factory.reset();
        return true;
    }
} // namespace GameWIP::Window::Detail::Platform

#if INTERNAL_WINDOW_TEST_HOOKS
namespace GameWIP::Window::TestHooks
{
    std::uint32_t refreshRateMillihertz(std::uint32_t numerator, std::uint32_t denominator) noexcept
    {
        return Detail::Platform::rationalMillihertz({numerator, denominator});
    }

    void simulateDisplayColorConfigurationChange() noexcept
    {
        Detail::Platform::displayColorFactory.forceConfigurationChange = true;
    }

    void makeNextDisplayColorMetadataUnavailable() noexcept
    {
        Detail::Platform::displayColorFactory.forceMetadataUnavailable = true;
    }
} // namespace GameWIP::Window::TestHooks
#endif
