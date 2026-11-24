/// @file win32_hid_reports.cpp
/// @brief Win32 HID report decoding and value publication.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// HID value decoding
    /// ------------------------------------------------------------

    [[nodiscard]] static std::int64_t signExtendHidValue(ULONG value, std::uint16_t bitSize) noexcept
    {
        const std::uint16_t width = std::min<std::uint16_t>(bitSize, 32);
        if(width == 0 || width == 32 || (value & (static_cast<ULONG>(1U) << static_cast<ULONG>(width - 1U))) == 0)
        {
            return static_cast<std::int64_t>(static_cast<std::int32_t>(value));
        }

        const ULONG mask = ~static_cast<ULONG>(0) << width;
        return static_cast<std::int64_t>(static_cast<std::int32_t>(value | mask));
    }

    [[nodiscard]] static Types::Value hidValue(const Attachment::Impl::DeviceBinding::HidElement &element, ULONG value) noexcept
    {
        if(element.signedValue)
        {
            return signExtendHidValue(value, element.bitSize);
        }

        return static_cast<std::uint64_t>(value);
    }

    /// ------------------------------------------------------------
    /// Normalized gamepad and HID report publication
    /// ------------------------------------------------------------

    [[nodiscard]] static float normalizedHidValue(const Attachment::Impl::DeviceBinding::HidElement &element, const Types::Value &value) noexcept
    {
        double raw = 0.0;
        if(const auto *signedValue = std::get_if<std::int64_t>(&value); signedValue != nullptr)
        {
            raw = static_cast<double>(*signedValue);
        }
        else if(const auto *unsignedValue = std::get_if<std::uint64_t>(&value); unsignedValue != nullptr)
        {
            raw = static_cast<double>(*unsignedValue);
        }
        else
        {
            return 0.0F;
        }

        const double minimum = static_cast<double>(element.logicalMinimum);
        const double maximum = static_cast<double>(element.logicalMaximum);
        if(maximum <= minimum)
        {
            return 0.0F;
        }

        return std::clamp(static_cast<float>((raw - minimum) / (maximum - minimum)), 0.0F, 1.0F);
    }

    static void publishNormalizedGamepad(
        System::Impl &state,
        DeviceBinding &device,
        const Attachment::Impl::DeviceBinding::HidElement &source,
        const Types::Value &value) noexcept
    {
        const float normalized = normalizedHidValue(source, value);
        for(Attachment::Impl::DeviceBinding::NormalizedElement &element : device.normalizedElements)
        {
            if(element.isHat)
            {
                if(element.sourceX != source.id)
                {
                    continue;
                }
                std::uint64_t hat = 8;
                if(const auto *signedValue = std::get_if<std::int64_t>(&value); signedValue != nullptr && *signedValue >= 0)
                {
                    hat = static_cast<std::uint64_t>(*signedValue);
                }
                else if(const auto *unsignedValue = std::get_if<std::uint64_t>(&value); unsignedValue != nullptr)
                {
                    hat = *unsignedValue;
                }
                const bool centered = hat > 7;
                const bool pressed = !centered && ((element.hatDirection == 0 && (hat == 0 || hat == 1 || hat == 7)) ||
                                                   (element.hatDirection == 1 && (hat == 1 || hat == 2 || hat == 3)) ||
                                                   (element.hatDirection == 2 && (hat == 3 || hat == 4 || hat == 5)) ||
                                                   (element.hatDirection == 3 && (hat == 5 || hat == 6 || hat == 7)));
                publish(state, element.id, pressed, false);
                continue;
            }

            if(element.standard == Standard::Gamepad::LeftTrigger || element.standard == Standard::Gamepad::RightTrigger)
            {
                if(element.sourceX == source.id)
                {
                    publish(state, element.id, normalized, false);
                }
                continue;
            }

            bool changed = false;
            if(element.sourceX == source.id)
            {
                element.x = normalized * 2.0F - 1.0F;
                changed = true;
            }

            if(element.sourceY == source.id)
            {
                element.y = normalized * 2.0F - 1.0F;
                changed = true;
            }

            if(changed)
            {
                publish(state, element.id, Types::Vector2F32{element.x, element.y}, false);
            }
        }
    }

    void publishHidReport(System::Impl &state, DeviceBinding &device, std::span<const std::byte> report) noexcept
    {
        appendRawReport(state, state.configuration, device.id, Types::BackendKind::Win32Hid, report);

        if(report.empty())
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        if(device.hidOpaque)
        {
            return;
        }

        if(device.hidPreparsedData.empty())
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }
        auto preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(device.hidPreparsedData.data());
        ULONG usageLength = 0;
        const BYTE reportId = std::to_integer<BYTE>(report.front());
        const bool hasButtons = std::any_of(
            device.hidElements.begin(),
            device.hidElements.end(),
            [reportId](const Attachment::Impl::DeviceBinding::HidElement &element)
            {
                return element.button && (element.reportId == 0 || element.reportId == reportId);
            });
        if(hasButtons)
        {
            usageLength = static_cast<ULONG>(device.hidUsageScratch.size());
            const NTSTATUS usageStatus = HidP_GetUsagesEx(
                HidP_Input,
                HIDP_LINK_COLLECTION_UNSPECIFIED,
                device.hidUsageScratch.data(),
                &usageLength,
                preparsed,
                reinterpret_cast<PCHAR>(const_cast<std::byte *>(report.data())),
                static_cast<ULONG>(report.size()));
            if(usageStatus != HIDP_STATUS_SUCCESS && usageStatus != HIDP_STATUS_BUTTON_NOT_PRESSED)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                return;
            }
        }

        for(const Attachment::Impl::DeviceBinding::HidElement &element : device.hidElements)
        {
            if(element.reportId != 0 && reportId != element.reportId)
            {
                continue;
            }

            if(element.button)
            {
                bool pressed = false;
                for(ULONG index = 0; index < usageLength; ++index)
                {
                    if(device.hidUsageScratch[index].UsagePage == element.usagePage && device.hidUsageScratch[index].Usage == element.usage)
                    {
                        pressed = true;
                        break;
                    }
                }
                publish(state, element.id, pressed, false);
                continue;
            }

            ULONG rawValue = 0;
            const NTSTATUS valueStatus = HidP_GetUsageValue(
                HidP_Input,
                element.usagePage,
                element.linkCollection,
                element.usage,
                &rawValue,
                preparsed,
                reinterpret_cast<PCHAR>(const_cast<std::byte *>(report.data())),
                static_cast<ULONG>(report.size()));
            if(valueStatus != HIDP_STATUS_SUCCESS)
            {
                continue;
            }
            const Types::Value value = hidValue(element, rawValue);
            publish(state, element.id, value, !element.absolute);
            publishNormalizedGamepad(state, device, element, value);
        }
    }

} // namespace GameWIP::Input::Platform::Win32
