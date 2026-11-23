/// @file win32_hid.cpp
/// @brief Win32 HID device discovery and control descriptions.

#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"
#include "input/platform/win32/internal/device_registration.h"
#include "input/platform/win32/internal/win32_attachment_state.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// HID control mapping
    /// ------------------------------------------------------------

    [[nodiscard]] static std::optional<Types::StandardId> standardFromHid(
        USAGE usagePage,
        USAGE usage,
        USAGE topLevelUsagePage,
        USAGE topLevelUsage) noexcept
    {
        constexpr USAGE keyboardPage = 0x07;
        constexpr USAGE genericDesktopPage = 0x01;
        constexpr USAGE buttonPage = 0x09;

        if(usagePage == keyboardPage &&
           ((usage >= 0x04 && usage <= 0x31) || (usage >= 0x33 && usage <= 0x63) || usage == 0x65 || (usage >= 0xE0 && usage <= 0xE7)))
        {
            return Types::StandardId{Types::StandardDomain::Keyboard, usage};
        }

        if(topLevelUsagePage == genericDesktopPage && topLevelUsage == 0x02 && usagePage == buttonPage)
        {
            switch(usage)
            {
            case 0x01:
                return Standard::Mouse::Left;
            case 0x02:
                return Standard::Mouse::Right;
            case 0x03:
                return Standard::Mouse::Middle;
            case 0x04:
                return Standard::Mouse::X1;
            case 0x05:
                return Standard::Mouse::X2;
            case 0x06:
                return Standard::Mouse::Button6;
            case 0x07:
                return Standard::Mouse::Button7;
            case 0x08:
                return Standard::Mouse::Button8;
            case 0x09:
                return Standard::Mouse::Button9;
            case 0x0A:
                return Standard::Mouse::Button10;
            case 0x0B:
                return Standard::Mouse::Button11;
            case 0x0C:
                return Standard::Mouse::Button12;
            case 0x0D:
                return Standard::Mouse::Button13;
            case 0x0E:
                return Standard::Mouse::Button14;
            case 0x0F:
                return Standard::Mouse::Button15;
            case 0x10:
                return Standard::Mouse::Button16;
            default:
                break;
            }
        }

        // Conventional HID gamepads commonly use button usages in physical
        // order. The original HID identity remains attached so unusual
        // layouts are still bindable without relying on this convention.
        if(topLevelUsagePage == genericDesktopPage && topLevelUsage == 0x04 && usagePage == buttonPage)
        {
            switch(usage)
            {
            case 0x01:
                return Standard::Gamepad::South;
            case 0x02:
                return Standard::Gamepad::East;
            case 0x03:
                return Standard::Gamepad::West;
            case 0x04:
                return Standard::Gamepad::North;
            case 0x05:
                return Standard::Gamepad::LeftShoulder;
            case 0x06:
                return Standard::Gamepad::RightShoulder;
            case 0x07:
                return Standard::Gamepad::Back;
            case 0x08:
                return Standard::Gamepad::Start;
            case 0x09:
                return Standard::Gamepad::LeftStickButton;
            case 0x0A:
                return Standard::Gamepad::RightStickButton;
            case 0x0B:
                return Standard::Gamepad::Guide;
            default:
                break;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] static Types::NumericStorage hidNumericStorage(bool signedValue, std::uint16_t bitSize) noexcept
    {
        if(signedValue)
        {
            if(bitSize <= 8)
            {
                return Types::NumericStorage::Signed8;
            }

            if(bitSize <= 16)
            {
                return Types::NumericStorage::Signed16;
            }

            if(bitSize <= 32)
            {
                return Types::NumericStorage::Signed32;
            }
            return Types::NumericStorage::Signed64;
        }

        if(bitSize <= 8)
        {
            return Types::NumericStorage::Unsigned8;
        }

        if(bitSize <= 16)
        {
            return Types::NumericStorage::Unsigned16;
        }

        if(bitSize <= 32)
        {
            return Types::NumericStorage::Unsigned32;
        }

        return Types::NumericStorage::Unsigned64;
    }

    [[nodiscard]] static Types::Scalar hidMinimum(LONG value, bool signedValue) noexcept
    {
        return signedValue ? Types::Scalar{static_cast<std::int64_t>(value)} : Types::Scalar{static_cast<std::uint64_t>(static_cast<ULONG>(value))};
    }

    [[nodiscard]] static Types::Scalar hidMaximum(LONG value, bool signedValue) noexcept
    {
        return signedValue ? Types::Scalar{static_cast<std::int64_t>(value)} : Types::Scalar{static_cast<std::uint64_t>(static_cast<ULONG>(value))};
    }

    [[nodiscard]] static std::string hidElementName(USAGE usagePage, USAGE usage, std::uint32_t component)
    {
        return "HID Usage " + std::to_string(usagePage) + ":" + std::to_string(usage) + "." + std::to_string(component);
    }

    /// ------------------------------------------------------------
    /// Gamepad normalization
    /// ------------------------------------------------------------

    static void addNormalizedGamepadElements(
        System::Impl &state,
        Types::DeviceInfo &descriptor,
        DeviceBinding &binding,
        const HIDP_CAPS &capabilities,
        std::string_view persistentKey)
    {
        // The HID gamepad convention is X/Y for the left stick, Rx/Ry for
        // the right stick, Z/Rz for the triggers, and usage 0x39 for the
        // hat. Raw elements remain available when a device differs.
        if(capabilities.UsagePage != 0x01 || capabilities.Usage != 0x04 || descriptor.elements.size() > 4080)
        {
            return;
        }
        const auto findValue = [&binding](USAGE usage) -> const Attachment::Impl::DeviceBinding::HidElement *
        {
            const auto element = std::find_if(
                binding.hidElements.begin(),
                binding.hidElements.end(),
                [usage](const Attachment::Impl::DeviceBinding::HidElement &candidate)
                {
                    return !candidate.button && candidate.absolute && candidate.usagePage == 0x01 && candidate.usage == usage;
                });
            return element == binding.hidElements.end() ? nullptr : &(*element);
        };

        const auto normalizedKey = [persistentKey](Types::StandardId standard)
        {
            const std::string key = standardKey(standard);
            return persistentKey.empty() ? key : std::string(persistentKey) + ":" + key;
        };

        const auto addVector = [&](Types::StandardId standard, std::string_view name, USAGE xUsage, USAGE yUsage)
        {
            const auto *x = findValue(xUsage);
            const auto *y = findValue(yUsage);
            if(x == nullptr || y == nullptr)
            {
                return;
            }

            const Types::ElementId id = addElement(
                state,
                descriptor,
                binding,
                std::string(name),
                normalizedKey(standard),
                standard,
                Types::ValueKind::Vector2F32,
                Types::ReportingMode::Absolute,
                {component(0, "X", numericInfo(Types::NumericStorage::Float32, -1.0F, 1.0F, Types::Unit::NormalizedByBackend)),
                 component(1, "Y", numericInfo(Types::NumericStorage::Float32, -1.0F, 1.0F, Types::Unit::NormalizedByBackend))},
                ElementRole::Standard);
            binding.normalizedElements.push_back({
                .id = id,
                .sourceX = x->id,
                .sourceY = y->id,
                .standard = standard,
            });
        };

        const auto addScalar = [&](Types::StandardId standard, std::string_view name, USAGE usage)
        {
            const auto *source = findValue(usage);
            if(source == nullptr)
            {
                return;
            }

            const Types::ElementId id = addElement(
                state,
                descriptor,
                binding,
                std::string(name),
                normalizedKey(standard),
                standard,
                Types::ValueKind::Float32,
                Types::ReportingMode::Absolute,
                {component(0, "Value", numericInfo(Types::NumericStorage::Float32, 0.0F, 1.0F, Types::Unit::NormalizedByBackend))},
                ElementRole::Trigger);
            binding.normalizedElements.push_back({
                .id = id,
                .sourceX = source->id,
                .standard = standard,
            });
        };

        addVector(Standard::Gamepad::LeftStick, "Left Stick", 0x30, 0x31);
        addVector(Standard::Gamepad::RightStick, "Right Stick", 0x33, 0x34);
        addScalar(Standard::Gamepad::LeftTrigger, "Left Trigger", 0x32);
        addScalar(Standard::Gamepad::RightTrigger, "Right Trigger", 0x35);

        const auto *hat = findValue(0x39);
        if(hat != nullptr)
        {
            const std::array<std::pair<Types::StandardId, std::string_view>, 4> directions{
                {{Standard::Gamepad::DpadUp, "D-pad Up"},
                 {Standard::Gamepad::DpadRight, "D-pad Right"},
                 {Standard::Gamepad::DpadDown, "D-pad Down"},
                 {Standard::Gamepad::DpadLeft, "D-pad Left"}}};
            for(std::size_t index = 0; index < directions.size(); ++index)
            {
                const auto &[standard, name] = directions[index];
                const Types::ElementId id = addElement(
                    state,
                    descriptor,
                    binding,
                    std::string(name),
                    normalizedKey(standard),
                    standard,
                    Types::ValueKind::Boolean,
                    Types::ReportingMode::Absolute,
                    {},
                    ElementRole::Standard);
                binding.normalizedElements.push_back({
                    .id = id,
                    .sourceX = hat->id,
                    .standard = standard,
                    .hatDirection = static_cast<std::uint8_t>(index),
                    .isHat = true,
                });
            }
        }
    }

    /// ------------------------------------------------------------
    /// HID device discovery
    /// ------------------------------------------------------------

    [[nodiscard]] bool addHidDevice(System::Impl &state, Attachment::Impl &attachment, HANDLE nativeDevice)
    {
        if(DeviceBinding *existing = findHidDevice(attachment, nativeDevice); existing != nullptr)
        {
            reconnectDevice(state, *existing);
            return true;
        }

        RID_DEVICE_INFO nativeInfo{};
        HIDP_CAPS capabilities{};
        std::vector<std::byte> preparsedData;
        if(!hidDeviceInfo(nativeDevice, nativeInfo) || !hidPreparsedData(nativeDevice, preparsedData, capabilities))
        {
            return false;
        }

        if(capabilities.InputReportByteLength == 0)
        {
            return false;
        }

        std::wstring nativePath;
        const std::string nativeName = rawDeviceName(nativeDevice, &nativePath);
        const HidDeviceStrings deviceStrings = queryHidDeviceStrings(nativePath);
        const std::string sourcePersistentKey = nativeName.empty() ? std::string{} : "win32:device:path:" + nativeName;
        const std::string containerKey = containerPersistentKey(nativePath);
        const std::string logicalPersistentKey = containerKey.empty() ? sourcePersistentKey : containerKey;
        const Types::IdentityConfidence logicalIdentityConfidence =
            sourcePersistentKey.empty()
                ? Types::IdentityConfidence::None
                : (containerKey.empty() ? Types::IdentityConfidence::ConnectionPath : Types::IdentityConfidence::DeviceUnique);

        DeviceRegistrationRollback transaction{state, attachment};

        try
        {
            // Find or create the shared logical device.
            Types::DeviceInfo *logicalDescriptor = findLogicalDevice(state, logicalPersistentKey);
            const bool newLogicalDevice = logicalDescriptor == nullptr;
            const bool logicalWasConnected = logicalDescriptor != nullptr && logicalDescriptor->connected;

            if(!newLogicalDevice)
            {
                transaction.saveMergedDevice(*logicalDescriptor);
            }

            // Build or merge the logical descriptor and source binding.
            const Types::DeviceId deviceId = newLogicalDevice ? Types::DeviceId{state.instance, state.nextDeviceSlot++, 1} : logicalDescriptor->id;
            DeviceBinding binding{
                .role = DeviceRole::Hid,
                .nativeSlot = 0,
                .nativeDevice = nativeDevice,
                .id = deviceId,
                .connected = true,
                .sourceIndex = 0,
            };
            Types::SourceInfo source{
                .backend = Types::BackendKind::Win32Hid,
                .backendName = "Win32 HID",
                .active = true,
                .persistentKey = sourcePersistentKey,
                .identityConfidence = sourcePersistentKey.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
                .vendorId = static_cast<std::uint16_t>(nativeInfo.hid.dwVendorId),
                .productId = static_cast<std::uint16_t>(nativeInfo.hid.dwProductId),
            };
            if(newLogicalDevice)
            {
                Types::DeviceInfo nextDescriptor{
                    .id = deviceId,
                    .origin = Types::DeviceOrigin::Physical,
                    .name = deviceStrings.product.empty() ? hidDeviceFallbackName(true, nativeInfo.hid.usUsagePage, nativeInfo.hid.usUsage)
                                                          : deviceStrings.product,
                    .manufacturer = deviceStrings.manufacturer,
                    .persistentKey = logicalPersistentKey,
                    .identityConfidence = logicalIdentityConfidence,
                    .vendorId = source.vendorId,
                    .productId = source.productId,
                    .connected = true,
                    .descriptorRevision = ++state.deviceRevision,
                    .sources = {source},
                };
                state.devices.push_back(std::move(nextDescriptor));
                logicalDescriptor = &state.devices.back();
            }
            else
            {
                binding.sourceIndex = logicalDescriptor->sources.size();
                logicalDescriptor->sources.push_back(std::move(source));
                logicalDescriptor->connected = true;
                logicalDescriptor->descriptorRevision = ++state.deviceRevision;
                if(static_cast<unsigned int>(logicalIdentityConfidence) > static_cast<unsigned int>(logicalDescriptor->identityConfidence))
                {
                    logicalDescriptor->identityConfidence = logicalIdentityConfidence;
                }

                if(!logicalDescriptor->vendorId.has_value())
                {
                    logicalDescriptor->vendorId = logicalDescriptor->sources.back().vendorId;
                }

                if(!logicalDescriptor->productId.has_value())
                {
                    logicalDescriptor->productId = logicalDescriptor->sources.back().productId;
                }

                if(!deviceStrings.product.empty() && isGenericDeviceName(logicalDescriptor->name))
                {
                    logicalDescriptor->name = deviceStrings.product;
                }

                if(logicalDescriptor->manufacturer.empty() && !deviceStrings.manufacturer.empty())
                {
                    logicalDescriptor->manufacturer = deviceStrings.manufacturer;
                }
            }

            Types::DeviceInfo &descriptor = *logicalDescriptor;

            // Read and validate the HID button and value capabilities.
            constexpr std::size_t maximumElements = 4096;
            const std::size_t buttonCount = capabilities.NumberInputButtonCaps;
            const std::size_t valueCount = capabilities.NumberInputValueCaps;
            // Restore partial registration if the HID layout cannot be represented safely.
            if((buttonCount == 0 && valueCount == 0) || buttonCount > maximumElements || valueCount > maximumElements ||
               buttonCount + valueCount > maximumElements)
            {
                transaction.restore();
                return false;
            }
            std::vector<HIDP_BUTTON_CAPS> buttonCaps(buttonCount);
            std::vector<HIDP_VALUE_CAPS> valueCaps(valueCount);
            if(buttonCount != 0)
            {
                USHORT count = static_cast<USHORT>(buttonCount);
                if(HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &count, reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsedData.data())) !=
                   HIDP_STATUS_SUCCESS)
                {
                    transaction.restore();
                    return false;
                }
                buttonCaps.resize(count);
            }

            if(valueCount != 0)
            {
                USHORT count = static_cast<USHORT>(valueCount);
                if(HidP_GetValueCaps(HidP_Input, valueCaps.data(), &count, reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsedData.data())) !=
                   HIDP_STATUS_SUCCESS)
                {
                    transaction.restore();
                    return false;
                }
                valueCaps.resize(count);
            }

            descriptor.elements.reserve(std::min(maximumElements, buttonCount + valueCount));
            binding.elements.reserve(std::min(maximumElements, buttonCount + valueCount));
            binding.hidElements.reserve(std::min(maximumElements, buttonCount + valueCount));

            // Add button controls.
            auto addButton = [&](USAGE usage, const HIDP_BUTTON_CAPS &caps, std::uint32_t component)
            {
                const Types::HidControlId hid{
                    .usagePage = caps.UsagePage,
                    .usage = usage,
                    .collection = caps.LinkCollection,
                    .reportId = caps.ReportID,
                    .component = component,
                };
                const auto standard = standardFromHid(caps.UsagePage, usage, capabilities.UsagePage, capabilities.Usage);
                const Types::ElementId id = addElement(
                    state,
                    descriptor,
                    binding,
                    hidElementName(caps.UsagePage, usage, component),
                    sourcePersistentKey.empty()
                        ? std::string{}
                        : sourcePersistentKey + ":control:" + std::to_string(caps.UsagePage) + ":" + std::to_string(usage) + ":" +
                              std::to_string(caps.LinkCollection) + ":" + std::to_string(caps.ReportID) + ":" + std::to_string(component),
                    standard,
                    Types::ValueKind::Boolean,
                    Types::ReportingMode::Absolute,
                    {},
                    ElementRole::Standard,
                    hid);
                binding.hidElements.push_back({
                    .id = id,
                    .usagePage = caps.UsagePage,
                    .usage = usage,
                    .linkCollection = caps.LinkCollection,
                    .reportId = caps.ReportID,
                    .component = component,
                    .button = true,
                    .absolute = caps.IsAbsolute != FALSE,
                });
            };

            std::size_t elementCount = 0;
            for(const HIDP_BUTTON_CAPS &caps : buttonCaps)
            {
                const std::uint32_t first = caps.IsRange ? caps.Range.UsageMin : caps.NotRange.Usage;
                const std::uint32_t last = caps.IsRange ? caps.Range.UsageMax : first;
                if(last < first || last - first >= maximumElements || elementCount + (last - first + 1) > maximumElements)
                {
                    transaction.restore();
                    return false;
                }

                for(std::uint32_t usage = first; usage <= last; ++usage)
                {
                    addButton(static_cast<USAGE>(usage), caps, 0);
                    ++elementCount;
                }
            }

            // Add numeric controls.
            auto addValue = [&](USAGE usage, const HIDP_VALUE_CAPS &caps, std::uint32_t componentIndex)
            {
                const bool signedValue = caps.LogicalMin < 0;
                const Types::HidControlId hid{
                    .usagePage = caps.UsagePage,
                    .usage = usage,
                    .collection = caps.LinkCollection,
                    .reportId = caps.ReportID,
                    .component = componentIndex,
                };
                const auto standard = standardFromHid(caps.UsagePage, usage, capabilities.UsagePage, capabilities.Usage);
                const Types::NumericInfo numeric{
                    .nativeStorage = hidNumericStorage(signedValue, caps.BitSize),
                    .minimum = hidMinimum(caps.LogicalMin, signedValue),
                    .maximum = hidMaximum(caps.LogicalMax, signedValue),
                    .unit = Types::Unit::Unspecified,
                    .unitExponent = static_cast<std::int32_t>(caps.UnitsExp),
                    .nullStateSupported = caps.HasNull != FALSE,
                };
                const Types::ElementId id = addElement(
                    state,
                    descriptor,
                    binding,
                    hidElementName(caps.UsagePage, usage, componentIndex),
                    sourcePersistentKey.empty()
                        ? std::string{}
                        : sourcePersistentKey + ":control:" + std::to_string(caps.UsagePage) + ":" + std::to_string(usage) + ":" +
                              std::to_string(caps.LinkCollection) + ":" + std::to_string(caps.ReportID) + ":" + std::to_string(componentIndex),
                    standard,
                    signedValue ? Types::ValueKind::SignedInteger : Types::ValueKind::UnsignedInteger,
                    caps.IsAbsolute ? Types::ReportingMode::Absolute : Types::ReportingMode::Relative,
                    {component(0, "Value", numeric)},
                    ElementRole::Standard,
                    hid);
                binding.hidElements.push_back({
                    .id = id,
                    .usagePage = caps.UsagePage,
                    .usage = usage,
                    .linkCollection = caps.LinkCollection,
                    .reportId = caps.ReportID,
                    .component = componentIndex,
                    .button = false,
                    .absolute = caps.IsAbsolute != FALSE,
                    .signedValue = signedValue,
                    .logicalMinimum = caps.LogicalMin,
                    .logicalMaximum = caps.LogicalMax,
                    .bitSize = caps.BitSize,
                });
            };

            for(const HIDP_VALUE_CAPS &caps : valueCaps)
            {
                const std::uint32_t first = caps.IsRange ? caps.Range.UsageMin : caps.NotRange.Usage;
                const std::uint32_t last = caps.IsRange ? caps.Range.UsageMax : first;
                if(last < first || last - first >= maximumElements || elementCount + (last - first + 1) > maximumElements)
                {
                    transaction.restore();
                    return false;
                }
                std::uint32_t componentIndex = 0;
                for(std::uint32_t usage = first; usage <= last; ++usage)
                {
                    addValue(static_cast<USAGE>(usage), caps, componentIndex++);
                    ++elementCount;
                }
            }

            // Add normalized controller controls.
            addNormalizedGamepadElements(state, descriptor, binding, capabilities, sourcePersistentKey);

            // Retain parser state and allocate report scratch storage.
            binding.hidPreparsedData = std::move(preparsedData);
            binding.hidReportLength = capabilities.InputReportByteLength;
            const ULONG usageCapacity =
                HidP_MaxUsageListLength(HidP_Input, 0, reinterpret_cast<PHIDP_PREPARSED_DATA>(binding.hidPreparsedData.data()));
            if(usageCapacity > maximumElements)
            {
                transaction.restore();
                return false;
            }
            binding.hidUsageScratch.resize(std::max<ULONG>(usageCapacity, 1));

            // Attach the source and publish the descriptor change.
            attachment.devices.push_back(std::move(binding));
            publishDeviceRegistration(state, descriptor, newLogicalDevice, logicalWasConnected, true);
            return true;
        }
        catch(const std::bad_alloc &)
        {
            transaction.restore();
            return false;
        }
    }

    [[nodiscard]] bool addOpaqueHidDevice(System::Impl &state, Attachment::Impl &attachment, HANDLE nativeDevice)
    {
        if(DeviceBinding *existing = findHidDevice(attachment, nativeDevice); existing != nullptr)
        {
            reconnectDevice(state, *existing);
            return true;
        }

        RID_DEVICE_INFO nativeInfo{};
        const bool hasNativeInfo = hidDeviceInfo(nativeDevice, nativeInfo);
        std::wstring nativePath;
        const std::string nativeName = rawDeviceName(nativeDevice, &nativePath);
        const HidDeviceStrings deviceStrings = queryHidDeviceStrings(nativePath);
        const std::string sourcePersistentKey = nativeName.empty() ? std::string{} : "win32:device:path:" + nativeName;
        const std::string containerKey = containerPersistentKey(nativePath);
        const std::string logicalPersistentKey = containerKey.empty() ? sourcePersistentKey : containerKey;
        const Types::IdentityConfidence logicalIdentityConfidence =
            sourcePersistentKey.empty()
                ? Types::IdentityConfidence::None
                : (containerKey.empty() ? Types::IdentityConfidence::ConnectionPath : Types::IdentityConfidence::DeviceUnique);

        DeviceRegistrationRollback transaction{state, attachment};

        try
        {
            // Find or create the shared logical device.
            Types::DeviceInfo *logicalDescriptor = findLogicalDevice(state, logicalPersistentKey);
            const bool newLogicalDevice = logicalDescriptor == nullptr;
            const bool logicalWasConnected = logicalDescriptor != nullptr && logicalDescriptor->connected;

            if(!newLogicalDevice)
            {
                transaction.saveMergedDevice(*logicalDescriptor);
            }

            // Build or merge the opaque HID source binding.
            const Types::DeviceId deviceId = newLogicalDevice ? Types::DeviceId{state.instance, state.nextDeviceSlot++, 1} : logicalDescriptor->id;
            DeviceBinding binding{
                .role = DeviceRole::Hid,
                .nativeSlot = 0,
                .nativeDevice = nativeDevice,
                .id = deviceId,
                .connected = true,
                .sourceIndex = 0,
            };
            binding.hidOpaque = true;
            const std::string deviceName =
                deviceStrings.product.empty()
                    ? hidDeviceFallbackName(hasNativeInfo, hasNativeInfo ? nativeInfo.hid.usUsagePage : 0, hasNativeInfo ? nativeInfo.hid.usUsage : 0)
                    : deviceStrings.product;
            Types::SourceInfo source{
                .backend = Types::BackendKind::Win32Hid,
                .backendName = "Win32 HID",
                .active = true,
                .persistentKey = sourcePersistentKey,
                .identityConfidence = sourcePersistentKey.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
                .vendorId = hasNativeInfo ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(nativeInfo.hid.dwVendorId)} : std::nullopt,
                .productId = hasNativeInfo ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(nativeInfo.hid.dwProductId)} : std::nullopt,
            };
            if(newLogicalDevice)
            {
                Types::DeviceInfo nextDescriptor{
                    .id = deviceId,
                    .origin = Types::DeviceOrigin::Physical,
                    .name = deviceName,
                    .manufacturer = deviceStrings.manufacturer,
                    .persistentKey = logicalPersistentKey,
                    .identityConfidence = logicalIdentityConfidence,
                    .vendorId = source.vendorId,
                    .productId = source.productId,
                    .connected = true,
                    .descriptorRevision = ++state.deviceRevision,
                    .sources = {source},
                };
                state.devices.push_back(std::move(nextDescriptor));
                logicalDescriptor = &state.devices.back();
            }
            else
            {
                binding.sourceIndex = logicalDescriptor->sources.size();
                logicalDescriptor->sources.push_back(std::move(source));
                logicalDescriptor->connected = true;
                logicalDescriptor->descriptorRevision = ++state.deviceRevision;
                if(static_cast<unsigned int>(logicalIdentityConfidence) > static_cast<unsigned int>(logicalDescriptor->identityConfidence))
                {
                    logicalDescriptor->identityConfidence = logicalIdentityConfidence;
                }

                if(!deviceStrings.product.empty() && isGenericDeviceName(logicalDescriptor->name))
                {
                    logicalDescriptor->name = deviceStrings.product;
                }

                if(logicalDescriptor->manufacturer.empty() && !deviceStrings.manufacturer.empty())
                {
                    logicalDescriptor->manufacturer = deviceStrings.manufacturer;
                }
            }

            // Attach the source and publish the device change.
            attachment.devices.push_back(std::move(binding));
            Types::DeviceInfo &descriptor = *logicalDescriptor;
            publishDeviceRegistration(state, descriptor, newLogicalDevice, logicalWasConnected, true);
            return true;
        }
        catch(const std::bad_alloc &)
        {
            transaction.restore();
            return false;
        }
    }

    [[nodiscard]] IO::Types::Status registerRawInputCollection(Attachment::Impl &attachment, USHORT usagePage, USHORT usage, DWORD flags)
    {
        const auto registered =
            std::find(attachment.rawInputCollections.begin(), attachment.rawInputCollections.end(), std::pair<USHORT, USHORT>{usagePage, usage});
        if(registered != attachment.rawInputCollections.end())
        {
            return IO::successStatus();
        }

        RAWINPUTDEVICE device{
            .usUsagePage = usagePage,
            .usUsage = usage,
            .dwFlags = flags,
            .hwndTarget = reinterpret_cast<HWND>(attachment.windowHandle),
        };
        if(!RegisterRawInputDevices(&device, 1, sizeof(device)))
        {
            return nativeStatus(GetLastError());
        }

        try
        {
            attachment.rawInputCollections.emplace_back(usagePage, usage);
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            RAWINPUTDEVICE removal{
                .usUsagePage = usagePage,
                .usUsage = usage,
                .dwFlags = RIDEV_REMOVE,
                .hwndTarget = nullptr,
            };
            (void)RegisterRawInputDevices(&removal, 1, sizeof(removal));
            return status(ErrorCode::OutOfMemory);
        }
    }

    [[nodiscard]] IO::Types::Status registerHidCollections(Attachment::Impl &attachment)
    {
        UINT deviceCount = 0;
        if(GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return nativeStatus(GetLastError());
        }

        if(deviceCount == 0)
        {
            return IO::successStatus();
        }
        std::vector<RAWINPUTDEVICELIST> devices(static_cast<std::size_t>(deviceCount));
        UINT received = deviceCount;
        if(GetRawInputDeviceList(devices.data(), &received, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return nativeStatus(GetLastError());
        }

        for(std::size_t index = 0; index < static_cast<std::size_t>(received); ++index)
        {
            const RAWINPUTDEVICELIST &device = devices[index];
            if(device.dwType != RIM_TYPEHID)
            {
                continue;
            }

            RID_DEVICE_INFO info{};
            if(!hidDeviceInfo(device.hDevice, info) || info.hid.usUsagePage == HID_USAGE_PAGE_GENERIC)
            {
                continue;
            }
            const IO::Types::Status registrationStatus =
                registerRawInputCollection(attachment, info.hid.usUsagePage, info.hid.usUsage, RIDEV_DEVNOTIFY);
            if(!registrationStatus.ok())
            {
                return registrationStatus;
            }
        }
        return IO::successStatus();
    }

    [[nodiscard]] bool registerHidDeviceNotification(Attachment::Impl &attachment) noexcept
    {
        constexpr GUID hidInterfaceGuid{0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = hidInterfaceGuid;
        attachment.hidDeviceNotification =
            RegisterDeviceNotificationW(reinterpret_cast<HWND>(attachment.windowHandle), &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        return attachment.hidDeviceNotification != nullptr;
    }

    void unregisterHidDeviceNotification(Attachment::Impl &attachment) noexcept
    {
        if(attachment.hidDeviceNotification != nullptr)
        {
            (void)UnregisterDeviceNotification(attachment.hidDeviceNotification);
            attachment.hidDeviceNotification = nullptr;
        }
    }
} // namespace GameWIP::Input::Platform::Win32
