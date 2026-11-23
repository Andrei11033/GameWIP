/// @file shell.cpp
/// @brief Shared state and pixel conversion for Desktop's Win32 shell features.

#include "desktop/platform/shell.h"
#include "desktop/platform/win32/internal/win32_shell_helpers.h"
#include "desktop/platform/win32/internal/win32_unicode.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shell message-window tracking
    /// ------------------------------------------------------------

    namespace
    {
        // Native creation and destruction balance this owner-thread count.
        thread_local std::size_t shellMessageWindowCount = 0;
    } // namespace

    void retainShellMessageWindow() noexcept
    {
        ++shellMessageWindowCount;
    }

    void releaseShellMessageWindow() noexcept
    {
        --shellMessageWindowCount;
    }

    bool hasShellEventSources() noexcept
    {
        return shellMessageWindowCount != 0;
    }

    /// ------------------------------------------------------------
    /// Shell capabilities
    /// ------------------------------------------------------------

    Types::Result<Types::Shell::Capabilities> getShellCapabilities() noexcept
    {
        static const Types::Shell::Capabilities capabilities = []
        {
            Types::Shell::Capabilities value;
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::Taskbar);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TaskbarProgress);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TaskbarOverlayIcon);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TaskbarThumbnailButtons);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayIcon);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenu);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenuIcons);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenuAccelerators);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::TrayMenuBadges);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::Notifications);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::NotificationSound);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::JumpLists);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::RecentItems);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::FileAssociationRegistration);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::UriSchemeRegistration);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::RegistrationVerbs);
            value.flags |= std::uint64_t{1} << static_cast<unsigned int>(Types::Shell::Capability::RegistrationExtendedMetadata);
            return value;
        }();
        return {.status = IO::successStatus(), .value = capabilities};
    }

    /// ------------------------------------------------------------
    /// Icon pixel conversion
    /// ------------------------------------------------------------

    namespace Win32
    {
        void copyRgbaToBgra(std::span<const std::byte> source, std::byte *destination) noexcept
        {
            for(std::size_t offset = 0; offset < source.size(); offset += 4)
            {
                const std::array<std::byte, 4> pixel{source[offset + 2], source[offset + 1], source[offset], source[offset + 3]};
                destination = std::copy(pixel.begin(), pixel.end(), destination);
            }
        }

        HICON createIcon(const OwnedIconImage &image) noexcept
        {
            if(image.size.width == 0 || image.size.height == 0 || image.rgba8.empty())
            {
                return nullptr;
            }

            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = static_cast<LONG>(image.size.width);
            header.bV5Height = -static_cast<LONG>(image.size.height);
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            HDC screen = GetDC(nullptr);

            if(screen == nullptr)
            {
                return nullptr;
            }

            void *pixels = nullptr;
            HBITMAP color = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO *>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);

            if(color == nullptr || pixels == nullptr)
            {
                if(color != nullptr)
                {
                    DeleteObject(color);
                }

                return nullptr;
            }
            copyRgbaToBgra(image.rgba8, static_cast<std::byte *>(pixels));
            HBITMAP mask = CreateBitmap(static_cast<int>(image.size.width), static_cast<int>(image.size.height), 1, 1, nullptr);

            if(mask == nullptr)
            {
                DeleteObject(color);
                return nullptr;
            }
            ICONINFO iconInfo{TRUE, 0, 0, mask, color};
            HICON icon = CreateIconIndirect(&iconInfo);
            DeleteObject(mask);
            DeleteObject(color);
            return icon;
        }

        const OwnedIconImage &closestIcon(std::span<const OwnedIconImage> images, int desiredWidth, int desiredHeight) noexcept
        {
            return *std::min_element(
                images.begin(),
                images.end(),
                [desiredWidth, desiredHeight](const OwnedIconImage &left, const OwnedIconImage &right)
                {
                    const auto distance = [desiredWidth, desiredHeight](const OwnedIconImage &image)
                    {
                        return std::llabs(static_cast<long long>(image.size.width) - desiredWidth) +
                               std::llabs(static_cast<long long>(image.size.height) - desiredHeight);
                    };
                    return distance(left) < distance(right);
                });
        }

        IO::Types::Status convertTip(std::string_view text, std::array<wchar_t, 260> &output)
        {
            std::wstring converted;
            DWORD nativeCode = ERROR_SUCCESS;

            if(!utf8ToUtf16(text, converted, nativeCode))
            {
                return IO::makeStatus(IO::Types::ErrorCode::EncodingFailed, nativeCode);
            }

            if(converted.size() >= output.size())
            {
                return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
            }
            std::copy(converted.begin(), converted.end(), output.begin());
            output[converted.size()] = L'\0';
            return IO::successStatus();
        }
    } // namespace Win32
} // namespace GameWIP::Desktop::Platform
