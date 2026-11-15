/// @file win32_keyboard_mapping.h
/// @brief Win32 set-1 scan code to USB HID keyboard usage mapping.
#pragma once

#include <array>
#include <cstdint>

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// @brief Maps a raw Win32 scan code and prefix to a USB HID keyboard usage.
    [[nodiscard]] constexpr std::uint32_t keyboardUsageForScanCode(unsigned int scanCode, bool extended, bool extendedPause) noexcept
    {
        // Handle the two-byte Pause sequence first.
        if(extendedPause)
        {
            return 0x48;
        }

        // Map E0-prefixed navigation and modifier keys separately.
        if(extended)
        {
            switch(scanCode)
            {
            case 0x1C:
                return 0x58;
            case 0x1D:
                return 0xE4;
            case 0x35:
                return 0x54;
            case 0x37:
                return 0x46;
            case 0x38:
                return 0xE6;
            case 0x46:
                return 0x48;
            case 0x47:
                return 0x4A;
            case 0x48:
                return 0x52;
            case 0x49:
                return 0x4B;
            case 0x4B:
                return 0x50;
            case 0x4D:
                return 0x4F;
            case 0x4F:
                return 0x4D;
            case 0x50:
                return 0x51;
            case 0x51:
                return 0x4E;
            case 0x52:
                return 0x49;
            case 0x53:
                return 0x4C;
            case 0x5B:
                return 0xE3;
            case 0x5C:
                return 0xE7;
            case 0x5D:
                return 0x65;
            default:
                return 0;
            }
        }

        // Resolve non-extended usages outside the base lookup table.
        switch(scanCode)
        {
        case 0x64:
            return 0x68;
        case 0x65:
            return 0x69;
        case 0x66:
            return 0x6A;
        case 0x67:
            return 0x6B;
        case 0x68:
            return 0x6C;
        case 0x69:
            return 0x6D;
        case 0x6A:
            return 0x6E;
        case 0x6B:
            return 0x6F;
        case 0x6C:
            return 0x70;
        case 0x6D:
            return 0x71;
        case 0x6E:
            return 0x72;
        case 0x76:
            return 0x73;
        default:
            break;
        }

        // Set-1 scan codes 0x00 through 0x58, indexed by scan code.
        constexpr std::array<std::uint32_t, 0x59> set1ToHidUsage{
            // 0x00-0x07
            0x00,
            0x29,
            0x1E,
            0x1F,
            0x20,
            0x21,
            0x22,
            0x23,
            // 0x08-0x0F
            0x24,
            0x25,
            0x26,
            0x27,
            0x2D,
            0x2E,
            0x2A,
            0x2B,
            // 0x10-0x17
            0x14,
            0x1A,
            0x08,
            0x15,
            0x17,
            0x1C,
            0x18,
            0x0C,
            // 0x18-0x1F
            0x12,
            0x13,
            0x2F,
            0x30,
            0x28,
            0xE0,
            0x04,
            0x16,
            // 0x20-0x27
            0x07,
            0x09,
            0x0A,
            0x0B,
            0x0D,
            0x0E,
            0x0F,
            0x33,
            // 0x28-0x2F
            0x34,
            0x35,
            0xE1,
            0x31,
            0x1D,
            0x1B,
            0x06,
            0x19,
            // 0x30-0x37
            0x05,
            0x11,
            0x10,
            0x36,
            0x37,
            0x38,
            0xE5,
            0x55,
            // 0x38-0x3F
            0xE2,
            0x2C,
            0x39,
            0x3A,
            0x3B,
            0x3C,
            0x3D,
            0x3E,
            // 0x40-0x47
            0x3F,
            0x40,
            0x41,
            0x42,
            0x43,
            0x53,
            0x47,
            0x5F,
            // 0x48-0x4F
            0x60,
            0x61,
            0x56,
            0x5C,
            0x5D,
            0x5E,
            0x57,
            0x59,
            // 0x50-0x57
            0x5A,
            0x5B,
            0x62,
            0x63,
            0x46,
            0x00,
            0x64,
            0x44,
            // 0x58
            0x45,
        };

        return scanCode < set1ToHidUsage.size() ? set1ToHidUsage[scanCode] : 0;
    }
} // namespace GameWIP::Input::Platform::Win32::Detail
