/// @file display_config.cpp
/// @brief Shared Win32 display-configuration queries and unit conversion.

#include "desktop/platform/win32/internal/win32_display_config.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/internal/results.h"

#if DESKTOP_INTERNAL_TEST_HOOKS
#include "desktop/internal/desktop_test_hooks.h"
#endif

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace GameWIP::Desktop::Platform::Win32
{
    /// @brief Converts a display-configuration refresh rational to millihertz.
    [[nodiscard]] std::uint32_t rationalMillihertz(DISPLAYCONFIG_RATIONAL value) noexcept
    {
        if(value.Numerator == 0 || value.Denominator == 0)
        {
            return 0;
        }
        const std::uint64_t scaled = static_cast<std::uint64_t>(value.Numerator) * 1000U;
        const std::uint64_t rounded = (scaled + value.Denominator / 2U) / value.Denominator;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(rounded, std::numeric_limits<std::uint32_t>::max()));
    }

    /// @brief Tests whether a display-configuration scanline order is interlaced.
    [[nodiscard]] bool interlaced(DISPLAYCONFIG_SCANLINE_ORDERING ordering) noexcept
    {
        return ordering == DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED || ordering == DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_LOWERFIELDFIRST;
    }

    /// @brief Finds the active display-config path matching a GDI device name.
    [[nodiscard]] GameWIP::Status::Types::Status findActiveDisplayPath(std::wstring_view device, ActiveDisplayPath &result) noexcept
    {
        try
        {
            const std::wstring deviceName(device);
            constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;

            // Retry when the display topology changes between sizing and querying.
            for(unsigned int attempt = 0; attempt < 4; ++attempt)
            {
                UINT32 pathCount = 0;
                UINT32 modeCount = 0;
                LONG nativeResult = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);

                if(nativeResult != ERROR_SUCCESS)
                {
                    return statusFromWin32(GameWIP::Status::Types::Code::StatFailed, static_cast<DWORD>(nativeResult), "GetDisplayConfigBufferSizes");
                }
                std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
                std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
                nativeResult = QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);

                if(nativeResult == ERROR_INSUFFICIENT_BUFFER)
                {
                    continue;
                }

                if(nativeResult != ERROR_SUCCESS)
                {
                    return statusFromWin32(GameWIP::Status::Types::Code::StatFailed, static_cast<DWORD>(nativeResult), "QueryDisplayConfig");
                }
                paths.resize(pathCount);

                // Match the active source to the requested GDI device name.
                for(const DISPLAYCONFIG_PATH_INFO &path : paths)
                {
                    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{
                        .header = {
                            .type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                            .size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME),
                            .adapterId = path.sourceInfo.adapterId,
                            .id = path.sourceInfo.id}};
                    nativeResult = DisplayConfigGetDeviceInfo(&source.header);

                    if(nativeResult != ERROR_SUCCESS)
                    {
                        continue;
                    }

                    if(_wcsicmp(source.viewGdiDeviceName, deviceName.c_str()) == 0)
                    {
                        result.path = path;
                        return GameWIP::Status::Types::Status{};
                    }
                }

                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotFound);
            }

            return GameWIP::Desktop::Internal::statusFor(
                GameWIP::Status::Types::Code::ResourceBusy,
                ERROR_INSUFFICIENT_BUFFER,
                "display topology changed repeatedly during QueryDisplayConfig");
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
    }

} // namespace GameWIP::Desktop::Platform::Win32

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
{
    /// ------------------------------------------------------------
    /// Validation support
    /// ------------------------------------------------------------

    /// @brief Converts a refresh rational into the portable millihertz representation.
    std::uint32_t refreshRateMillihertz(std::uint32_t numerator, std::uint32_t denominator) noexcept
    {
        return Platform::Win32::rationalMillihertz({numerator, denominator});
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
