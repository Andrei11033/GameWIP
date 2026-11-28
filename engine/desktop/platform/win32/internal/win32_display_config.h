/// @file win32_display_config.h
/// @brief Private helpers for querying Windows display configuration.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "status/result.h"
#include "status/status.h"

#include <cstdint>
#include <string_view>

namespace GameWIP::Desktop::Platform::Win32
{
    struct ActiveDisplayPath
    {
        DISPLAYCONFIG_PATH_INFO path;
    };

    [[nodiscard]] GameWIP::Status::Types::Status findActiveDisplayPath(std::wstring_view device, ActiveDisplayPath &result) noexcept;
    [[nodiscard]] std::uint32_t rationalMillihertz(DISPLAYCONFIG_RATIONAL value) noexcept;
    [[nodiscard]] bool interlaced(DISPLAYCONFIG_SCANLINE_ORDERING ordering) noexcept;
} // namespace GameWIP::Desktop::Platform::Win32
