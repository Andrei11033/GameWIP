/// @file win32_display_color.cpp
/// @brief Win32 display color-space, HDR, and luminance queries.

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/window.h"
#include "desktop/platform/win32/internal/win32_compat.h"
#include "desktop/platform/win32/internal/win32_display_config.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <dxgi1_6.h>

#include <cstdint>
#include <new>
#include <string>

namespace GameWIP::Desktop::Platform
{
    namespace StatusTypes = GameWIP::Status::Types;

    using Win32::ActiveDisplayPath;
    using Win32::findActiveDisplayPath;

    /// ------------------------------------------------------------
    /// Display color metadata helpers
    /// ------------------------------------------------------------

    namespace
    {
        /// @brief Owns a COM interface returned by a DXGI query.
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
                if(value != nullptr)
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
            // Explicit cleanup keeps DXGI Release calls inside an active Desktop
            // operation instead of relying on process-exit thread-local teardown.
            IDXGIFactory1 *factory = nullptr;
            bool queried = false;
#if DESKTOP_INTERNAL_TEST_HOOKS
            bool forceConfigurationChange = false;
            bool forceMetadataUnavailable = false;
#endif
        };

        thread_local DisplayColorFactoryState displayColorFactory;

        /// @brief Lazily creates or refreshes the calling thread's DXGI factory.
        [[nodiscard]] bool ensureDisplayColorFactory() noexcept
        {
            if(displayColorFactory.factory != nullptr && displayColorFactory.factory->IsCurrent() != FALSE)
            {
                return true;
            }

            releaseDisplayColorResources();
            return SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void **>(&displayColorFactory.factory)));
        }

        /// @brief Adds DXGI output color metadata when the monitor is exposed by DXGI.
        void addDxgiColorMetadata(HMONITOR monitor, DisplayColorSnapshot &snapshot) noexcept
        {
            if(!ensureDisplayColorFactory())
            {
                return;
            }

            for(UINT adapterIndex = 0;; ++adapterIndex)
            {
                ComReference<IDXGIAdapter1> adapter;
                const HRESULT adapterResult = displayColorFactory.factory->EnumAdapters1(adapterIndex, adapter.put());

                if(adapterResult == DXGI_ERROR_NOT_FOUND)
                {
                    return;
                }

                if(FAILED(adapterResult))
                {
                    return;
                }

                for(UINT outputIndex = 0;; ++outputIndex)
                {
                    ComReference<IDXGIOutput> output;
                    const HRESULT outputResult = adapter->EnumOutputs(outputIndex, output.put());

                    if(outputResult == DXGI_ERROR_NOT_FOUND)
                    {
                        break;
                    }

                    if(FAILED(outputResult))
                    {
                        break;
                    }

                    DXGI_OUTPUT_DESC outputDescription{};

                    if(FAILED(output->GetDesc(&outputDescription)) || outputDescription.Monitor != monitor)
                    {
                        continue;
                    }

                    ComReference<IDXGIOutput6> output6;

                    if(FAILED(output->QueryInterface(IID_IDXGIOutput6, reinterpret_cast<void **>(output6.put()))))
                    {
                        return;
                    }
                    DXGI_OUTPUT_DESC1 colorDescription{};

                    if(FAILED(output6->GetDesc1(&colorDescription)))
                    {
                        return;
                    }

                    if(snapshot.bitsPerColorChannel == 0)
                    {
                        snapshot.bitsPerColorChannel = colorDescription.BitsPerColor;
                    }
                    snapshot.minimumLuminanceNits = colorDescription.MinLuminance;
                    snapshot.maximumLuminanceNits = colorDescription.MaxLuminance;
                    snapshot.maximumFullFrameLuminanceNits = colorDescription.MaxFullFrameLuminance;

                    if(colorDescription.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                    {
                        snapshot.activeColorSpace = Types::Display::ColorSpace::Hdr10Pq;
                        snapshot.wideColorGamutSupported = true;
                        snapshot.hdrSupported = true;
                        snapshot.hdrEnabled = true;
                    }
                    else if(
                        colorDescription.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
                        snapshot.activeColorSpace == Types::Display::ColorSpace::Unknown)
                    {
                        snapshot.activeColorSpace = Types::Display::ColorSpace::Srgb;
                    }

                    return;
                }
            }
        }

        /// @brief Adds advanced-color metadata using the available native query.
        void addDisplayConfigColorMetadata(const ActiveDisplayPath &active, DisplayColorSnapshot &snapshot) noexcept
        {
            Compat::AdvancedColorInfo2 advanced{
                .header = {
                    .type = Compat::kGetAdvancedColorInfo2,
                    .size = sizeof(Compat::AdvancedColorInfo2),
                    .adapterId = active.path.targetInfo.adapterId,
                    .id = active.path.targetInfo.id}};

            if(DisplayConfigGetDeviceInfo(&advanced.header) == ERROR_SUCCESS)
            {
                snapshot.hdrSupported = (advanced.flags & Compat::kHighDynamicRangeSupported) != 0;
                snapshot.hdrEnabled =
                    (advanced.flags & Compat::kHighDynamicRangeUserEnabled) != 0 || advanced.activeColorMode == Compat::AdvancedColorMode::Hdr;
                snapshot.wideColorGamutSupported = (advanced.flags & (Compat::kWideColorSupported | Compat::kHighDynamicRangeSupported)) != 0;
                snapshot.bitsPerColorChannel = advanced.bitsPerColorChannel;

                switch(advanced.activeColorMode)
                {
                case Compat::AdvancedColorMode::Sdr:
                    snapshot.activeColorSpace = Types::Display::ColorSpace::Srgb;
                    break;
                case Compat::AdvancedColorMode::WideColorGamut:
                    snapshot.activeColorSpace = Types::Display::ColorSpace::WideColorGamut;
                    break;
                case Compat::AdvancedColorMode::Hdr:
                    snapshot.activeColorSpace = Types::Display::ColorSpace::Hdr10Pq;
                    break;
                default:
                    if((advanced.flags & Compat::kAdvancedColorActive) == 0)
                    {
                        snapshot.activeColorSpace = Types::Display::ColorSpace::Srgb;
                    }
                    break;
                }

                if((advanced.flags & Compat::kWideColorUserEnabled) != 0 && snapshot.activeColorSpace == Types::Display::ColorSpace::Unknown)
                {
                    snapshot.activeColorSpace = Types::Display::ColorSpace::WideColorGamut;
                }
            }
            else
            {
                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO fallback{
                    .header = {
                        .type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,
                        .size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO),
                        .adapterId = active.path.targetInfo.adapterId,
                        .id = active.path.targetInfo.id}};

                if(DisplayConfigGetDeviceInfo(&fallback.header) == ERROR_SUCCESS)
                {
                    snapshot.wideColorGamutSupported = fallback.advancedColorSupported != 0;
                    snapshot.hdrSupported = fallback.advancedColorSupported != 0;
                    snapshot.hdrEnabled = fallback.advancedColorEnabled != 0;
                    snapshot.bitsPerColorChannel = fallback.bitsPerColorChannel;
                    snapshot.activeColorSpace =
                        fallback.advancedColorEnabled != 0 ? Types::Display::ColorSpace::Unknown : Types::Display::ColorSpace::Srgb;
                }
            }

            DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel{
                .header = {
                    .type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
                    .size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL),
                    .adapterId = active.path.targetInfo.adapterId,
                    .id = active.path.targetInfo.id}};

            if(DisplayConfigGetDeviceInfo(&whiteLevel.header) == ERROR_SUCCESS)
            {
                snapshot.sdrWhiteLevelMilli80Nits = whiteLevel.SDRWhiteLevel;
            }
        }

    } // namespace
    /// ------------------------------------------------------------
    /// Display color state
    /// ------------------------------------------------------------

    /// @brief Returns HDR, gamut, luminance, and SDR reference-white metadata.
    StatusTypes::Result<Types::Display::ColorInfo> getColorInfo(Types::Display::MonitorId monitor) noexcept
    {
        if(!monitor.isValid())
        {
            return StatusTypes::Result<Types::Display::ColorInfo>::failure(StatusTypes::Status(StatusTypes::Code::InvalidArgument));
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DisplayColorQuery))
        {
            return StatusTypes::Result<Types::Display::ColorInfo>::failure(StatusTypes::Status(StatusTypes::Code::StatFailed));
        }

        try
        {
            const std::wstring device = monitorDeviceName(monitor);

            if(device.empty())
            {
                return StatusTypes::Result<Types::Display::ColorInfo>::failure(StatusTypes::Status(StatusTypes::Code::NotFound));
            }
            const HMONITOR native = nativeMonitor(monitor);

            if(native == nullptr)
            {
                return StatusTypes::Result<Types::Display::ColorInfo>::failure(StatusTypes::Status(StatusTypes::Code::NotFound));
            }
            ActiveDisplayPath active;
            GameWIP::Status::Types::Status status = findActiveDisplayPath(device, active);

            if(!status.ok())
            {
                return StatusTypes::Result<Types::Display::ColorInfo>::failure(status);
            }
            const bool releaseAfterQuery = !hasOpenWindowsOnCurrentThread();
            displayColorFactory.queried = true;
            DisplayColorSnapshot snapshot;
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(displayColorFactory.forceMetadataUnavailable)
            {
                displayColorFactory.forceMetadataUnavailable = false;

                if(releaseAfterQuery)
                {
                    releaseDisplayColorResources();
                }

                return StatusTypes::Result<Types::Display::ColorInfo>::success(makeDisplayColorInfo(monitor, snapshot));
            }
#endif
            addDisplayConfigColorMetadata(active, snapshot);
            addDxgiColorMetadata(native, snapshot);

            if(releaseAfterQuery)
            {
                releaseDisplayColorResources();
            }

            return StatusTypes::Result<Types::Display::ColorInfo>::success(makeDisplayColorInfo(monitor, snapshot));
        }
        catch(const std::bad_alloc &)
        {
            return StatusTypes::Result<Types::Display::ColorInfo>::failure(StatusTypes::Status(StatusTypes::Code::OutOfMemory));
        }
        catch(...)
        {
            return StatusTypes::Result<Types::Display::ColorInfo>::failure(StatusTypes::Status(StatusTypes::Code::Unknown));
        }
    }

    /// @brief Consumes a test or native display-color configuration change signal.
    bool consumeDisplayColorConfigurationChange() noexcept
    {
#if DESKTOP_INTERNAL_TEST_HOOKS
        if(displayColorFactory.forceConfigurationChange)
        {
            displayColorFactory.forceConfigurationChange = false;
            releaseDisplayColorResources();
            return true;
        }

#endif
        if(!displayColorFactory.queried || displayColorFactory.factory == nullptr || displayColorFactory.factory->IsCurrent() != FALSE)
        {
            return false;
        }

        releaseDisplayColorResources();
        return true;
    }

    void releaseDisplayColorResources() noexcept
    {
        if(displayColorFactory.factory != nullptr)
        {
            displayColorFactory.factory->Release();
            displayColorFactory.factory = nullptr;
        }
        displayColorFactory.queried = false;
    }

    void abandonDisplayColorResources() noexcept
    {
        // DXGI Release may raise a facility exception from Windows thread-exit
        // cleanup. The process reclaims this exceptional-path reference after
        // ordinary standalone and final-window paths have released it eagerly.
        displayColorFactory.factory = nullptr;
        displayColorFactory.queried = false;
    }
} // namespace GameWIP::Desktop::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
{
    /// ------------------------------------------------------------
    /// Validation support
    /// ------------------------------------------------------------

    /// @brief Injects a display-color configuration change for validation.
    void simulateDisplayColorConfigurationChange() noexcept
    {
        Platform::displayColorFactory.forceConfigurationChange = true;
    }

    /// @brief Makes the next display-color metadata query report unavailable metadata.
    void makeNextDisplayColorMetadataUnavailable() noexcept
    {
        Platform::displayColorFactory.forceMetadataUnavailable = true;
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
