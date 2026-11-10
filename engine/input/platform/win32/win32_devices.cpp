/// @file win32_devices.cpp
/// @brief Win32 device discovery, descriptor construction, and HID report decoding.

#include "input/platform/win32/internal/win32_attachment_state.h"

#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <new>
#include <string>
#include <utility>

namespace GameWIP::Input::Platform::Win32::Detail
{
    // ------------------------------------------------------------
    // Shared identity and descriptor helpers
    // ------------------------------------------------------------
    std::atomic<Attachment::Impl *> processRegistrationOwner{nullptr};

    [[nodiscard]] IO::Types::Status status(ErrorCode code) noexcept
    {
        return IO::makeStatus(code);
    }

    [[nodiscard]] IO::Types::Status nativeStatus(std::uint32_t code) noexcept
    {
        return IO::makeStatus(ErrorCode::NativeFailure, static_cast<std::int64_t>(code));
    }

    [[nodiscard]] Types::Timestamp now() noexcept
    {
        return Types::Timestamp{.receivedAt = Types::Clock::now()};
    }

    [[nodiscard]] std::uint64_t surfaceValue(const void *windowHandle) noexcept
    {
        const auto value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(windowHandle));
        return value == 0 ? 1 : value;
    }

    [[nodiscard]] static bool sameStandard(Types::StandardId left, Types::StandardId right) noexcept
    {
        return left == right;
    }

    [[nodiscard]] static Types::BackendKind backendFor(DeviceRole role) noexcept
    {
        if(role == DeviceRole::Gamepad)
        {
            return Types::BackendKind::XInput;
        }
        if(role == DeviceRole::Hid)
        {
            return Types::BackendKind::Win32Hid;
        }
        if(role == DeviceRole::Pointer)
        {
            return Types::BackendKind::Win32WindowMessage;
        }
        return Types::BackendKind::Win32RawInput;
    }

    /// @brief Finds a logical device that can safely absorb another source.
    /// @details Only connection-path or device-unique identities may merge;
    /// model-only identities such as XInput slots are never guessed together.
    [[nodiscard]] static Types::DeviceInfo *findLogicalDevice(System::Impl &state, std::string_view persistentKey) noexcept
    {
        if(persistentKey.empty())
        {
            return nullptr;
        }
        for(Types::DeviceInfo &device : state.devices)
        {
            if(static_cast<unsigned int>(device.identityConfidence) >= static_cast<unsigned int>(Types::IdentityConfidence::ConnectionPath) &&
               device.persistentKey == persistentKey)
            {
                return &device;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static std::string defaultSourceName(DeviceRole role)
    {
        if(role == DeviceRole::Gamepad)
        {
            return "XInput";
        }
        if(role == DeviceRole::Pointer)
        {
            return "Win32 Window Messages";
        }
        return role == DeviceRole::Hid ? "Win32 HID" : "Win32 Raw Input";
    }

    [[nodiscard]] static std::string deviceKey(DeviceRole role, std::uint32_t slot)
    {
        switch(role)
        {
        case DeviceRole::Keyboard:
        case DeviceRole::Mouse:
        case DeviceRole::Pointer:
            return {};
        case DeviceRole::Gamepad:
            return "xinput:slot:" + std::to_string(slot);
        case DeviceRole::Hid:
            return {};
        }
        return "win32:device";
    }

    [[nodiscard]] static std::string standardKey(Types::StandardId standard)
    {
        return "standard:" + std::to_string(static_cast<unsigned int>(standard.domain)) + ":" + std::to_string(standard.code);
    }

    [[nodiscard]] static Types::NumericInfo numericInfo(
        Types::NumericStorage storage,
        std::optional<Types::Scalar> minimum,
        std::optional<Types::Scalar> maximum,
        Types::Unit unit) noexcept
    {
        return Types::NumericInfo{
            .nativeStorage = storage,
            .minimum = minimum,
            .maximum = maximum,
            .unit = unit,
        };
    }

    [[nodiscard]] static Types::ComponentInfo component(std::uint8_t index, std::string_view name, Types::NumericInfo numeric)
    {
        return Types::ComponentInfo{
            .index = index,
            .name = std::string(name),
            .numeric = numeric,
        };
    }

    [[nodiscard]] Types::ElementState *findState(System::Impl &state, Types::ElementId id) noexcept
    {
        const std::size_t index = state.findStateIndex(id);
        return index == state.states.size() ? nullptr : &state.states[index];
    }

    [[nodiscard]] Types::ElementInfo *findElement(System::Impl &state, Types::ElementId id) noexcept
    {
        return state.findElement(id);
    }

    [[nodiscard]] DeviceBinding *findDevice(Attachment::Impl &attachment, DeviceRole role, std::uint32_t slot) noexcept
    {
        auto entry = std::find_if(
            attachment.devices.begin(),
            attachment.devices.end(),
            [role, slot](const DeviceBinding &device)
            {
                return device.role == role && device.nativeSlot == slot;
            });
        return entry == attachment.devices.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] DeviceBinding *findNativeDevice(Attachment::Impl &attachment, DeviceRole role, void *nativeDevice) noexcept
    {
        auto entry = std::find_if(
            attachment.devices.begin(),
            attachment.devices.end(),
            [role, nativeDevice](const DeviceBinding &device)
            {
                return device.role == role && device.nativeDevice == nativeDevice;
            });
        return entry == attachment.devices.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] DeviceBinding *findHidDevice(Attachment::Impl &attachment, void *nativeDevice) noexcept
    {
        return findNativeDevice(attachment, DeviceRole::Hid, nativeDevice);
    }

    [[nodiscard]] ElementBinding *findElement(DeviceBinding &device, Types::StandardId standard) noexcept
    {
        auto entry = std::find_if(
            device.elements.begin(),
            device.elements.end(),
            [standard](const ElementBinding &element)
            {
                return sameStandard(element.standard, standard);
            });
        return entry == device.elements.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] ElementBinding *findHidElement(DeviceBinding &device, Types::HidControlId hid) noexcept
    {
        auto entry = std::find_if(
            device.elements.begin(),
            device.elements.end(),
            [hid](const ElementBinding &element)
            {
                return element.hid == hid;
            });
        return entry == device.elements.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] ElementBinding *findRole(DeviceBinding &device, ElementRole role) noexcept
    {
        auto entry = std::find_if(
            device.elements.begin(),
            device.elements.end(),
            [role](const ElementBinding &element)
            {
                return element.role == role;
            });
        return entry == device.elements.end() ? nullptr : &(*entry);
    }

    // ------------------------------------------------------------
    // Device source and descriptor lifecycle
    // ------------------------------------------------------------
    static void appendDeviceEvent(
        System::Impl &state,
        Types::EventKind kind,
        Types::EventCause cause,
        Types::DeviceId device,
        std::uint64_t revision) noexcept
    {
        state.appendEvent({
            .timestamp = now(),
            .kind = kind,
            .cause = cause,
            .data = Types::DeviceEvent{device, revision},
        });
    }

    void appendElementEvent(
        System::Impl &state,
        Types::ElementId id,
        Types::Value value,
        Types::Value previous,
        bool previousKnown,
        Types::EventCause cause) noexcept
    {
        state.appendEvent({
            .timestamp = now(),
            .kind = Types::EventKind::ElementChanged,
            .cause = cause,
            .data =
                Types::ElementEvent{
                    .id = id,
                    .value = value,
                    .previousValue = previous,
                    .previousKnown = previousKnown,
                },
        });
    }

    [[nodiscard]] static Types::SourceInfo *sourceInfo(System::Impl &state, const DeviceBinding &device) noexcept
    {
        const std::size_t deviceIndex = state.findDeviceIndex(device.id);
        if(deviceIndex == state.devices.size() || device.sourceIndex >= state.devices[deviceIndex].sources.size())
        {
            return nullptr;
        }
        return &state.devices[deviceIndex].sources[device.sourceIndex];
    }

    [[nodiscard]] static bool hasActiveSource(const Types::DeviceInfo &device) noexcept
    {
        return std::any_of(
            device.sources.begin(),
            device.sources.end(),
            [](const Types::SourceInfo &source)
            {
                return source.active;
            });
    }

    [[nodiscard]] static bool hasActiveElementSource(const Attachment::Impl &attachment, Types::DeviceId device, Types::ElementId element) noexcept
    {
        return std::any_of(
            attachment.devices.begin(),
            attachment.devices.end(),
            [device, element](const DeviceBinding &candidate)
            {
                if(!candidate.connected || candidate.id != device)
                {
                    return false;
                }
                return std::any_of(
                    candidate.elements.begin(),
                    candidate.elements.end(),
                    [element](const ElementBinding &binding)
                    {
                        return binding.id == element;
                    });
            });
    }

    void invalidateDevice(System::Impl &state, Attachment::Impl &attachment, DeviceBinding &device, Types::EventCause cause) noexcept
    {
        if(!device.connected)
        {
            return;
        }

        const std::size_t deviceIndex = state.findDeviceIndex(device.id);
        if(deviceIndex == state.devices.size())
        {
            device.connected = false;
            return;
        }

        device.connected = false;
        if(Types::SourceInfo *source = sourceInfo(state, device); source != nullptr)
        {
            source->active = false;
        }
        Types::DeviceInfo &descriptor = state.devices[deviceIndex];
        descriptor.connected = hasActiveSource(descriptor);
        descriptor.descriptorRevision = ++state.deviceRevision;
        for(const Types::ElementInfo &element : descriptor.elements)
        {
            if(hasActiveElementSource(attachment, descriptor.id, element.id))
            {
                continue;
            }
            Types::ElementState *elementState = findState(state, element.id);
            if(elementState == nullptr)
            {
                continue;
            }
            elementState->validity = Types::ValueValidity::Unavailable;
            state.appendEvent({
                .timestamp = now(),
                .kind = Types::EventKind::ElementInvalidated,
                .cause = cause,
                .data =
                    Types::ElementEvent{
                        .id = element.id,
                        .value = elementState->value,
                        .previousValue = elementState->value,
                        .previousKnown = false,
                    },
            });
        }
        if(descriptor.connected)
        {
            appendDeviceEvent(state, Types::EventKind::DeviceChanged, cause, descriptor.id, descriptor.descriptorRevision);
            return;
        }
        if(state.health.activeDeviceCount != 0)
        {
            --state.health.activeDeviceCount;
        }
        appendDeviceEvent(state, Types::EventKind::DeviceDisconnected, cause, device.id, state.deviceRevision);
    }

    void reconnectDevice(System::Impl &state, DeviceBinding &device) noexcept
    {
        if(device.connected)
        {
            return;
        }
        const std::size_t deviceIndex = state.findDeviceIndex(device.id);
        if(deviceIndex == state.devices.size())
        {
            return;
        }

        Types::DeviceInfo &descriptor = state.devices[deviceIndex];
        const bool logicalWasConnected = descriptor.connected;
        device.connected = true;
        if(Types::SourceInfo *source = sourceInfo(state, device); source != nullptr)
        {
            source->active = true;
        }
        descriptor.connected = true;
        descriptor.descriptorRevision = ++state.deviceRevision;
        for(const ElementBinding &binding : device.elements)
        {
            Types::ElementState *elementState = findState(state, binding.id);
            if(elementState != nullptr && elementState->validity == Types::ValueValidity::Unavailable)
            {
                elementState->validity = Types::ValueValidity::Unknown;
            }
        }
        if(!logicalWasConnected)
        {
            ++state.health.activeDeviceCount;
            appendDeviceEvent(state, Types::EventKind::DeviceConnected, Types::EventCause::Resynchronized, device.id, descriptor.descriptorRevision);
        }
        else
        {
            appendDeviceEvent(state, Types::EventKind::DeviceChanged, Types::EventCause::SourceChanged, device.id, descriptor.descriptorRevision);
        }
    }

    Types::ElementId addElement(
        System::Impl &state,
        Types::DeviceInfo &device,
        DeviceBinding &binding,
        std::string name,
        std::string key,
        std::optional<Types::StandardId> standard,
        Types::ValueKind kind,
        Types::ReportingMode reporting,
        std::vector<Types::ComponentInfo> components,
        ElementRole role,
        std::optional<Types::HidControlId> hid)
    {
        if(!hid.has_value() && standard.has_value())
        {
            if(standard->domain == Types::StandardDomain::Keyboard)
            {
                hid = Types::HidControlId{.usagePage = 0x07, .usage = standard->code};
            }
            else if(standard->domain == Types::StandardDomain::Mouse && standard->code <= 0x05)
            {
                hid = Types::HidControlId{.usagePage = 0x09, .usage = standard->code};
            }
        }
        const Types::ControlIdentity identity{.standard = standard, .hid = hid};
        if(identity.isValid())
        {
            const DeviceRole bindingRole = binding.role;
            const auto existing = std::find_if(
                device.elements.begin(),
                device.elements.end(),
                [&identity, standard, bindingRole](const Types::ElementInfo &candidate)
                {
                    if(candidate.identity == identity)
                    {
                        return true;
                    }
                    // Raw keyboard/mouse and HID collections can describe
                    // the same standard control with different collection
                    // metadata. Share that canonical element only across
                    // those two backend representations; same-backend
                    // duplicates keep their HID identities distinct.
                    return standard.has_value() && candidate.identity.standard == standard &&
                           ((candidate.authoritativeBackend == Types::BackendKind::Win32RawInput && bindingRole == DeviceRole::Hid) ||
                            (candidate.authoritativeBackend == Types::BackendKind::Win32Hid && bindingRole != DeviceRole::Hid));
                });
            if(existing != device.elements.end())
            {
                binding.elements.push_back({
                    .id = existing->id,
                    .role = role,
                    .standard = standard.value_or(Types::StandardId{}),
                    .hid = hid.value_or(Types::HidControlId{}),
                });
                return existing->id;
            }
        }
        const Types::ElementId id{binding.id, state.nextElementSlot++, 1};
        device.elements.push_back({
            .id = id,
            .name = std::move(name),
            .persistentKey = std::move(key),
            .identity = Types::ControlIdentity{.standard = standard, .hid = hid},
            .kind = kind,
            .reporting = reporting,
            .components = std::move(components),
            .authoritativeBackend = backendFor(binding.role),
            .provenance = binding.role == DeviceRole::Gamepad ? Types::ValueProvenance::NativeApiProcessed : Types::ValueProvenance::NativeDecoded,
        });
        state.states.push_back({
            .id = id,
            .value = Internal::zeroValue(kind),
            .validity = Types::ValueValidity::Unknown,
        });
        binding.elements.push_back({
            .id = id,
            .role = role,
            .standard = standard.value_or(Types::StandardId{}),
            .hid = hid.value_or(Types::HidControlId{}),
        });
        return id;
    }

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

    // ------------------------------------------------------------
    // Logical device discovery and report decoding
    // ------------------------------------------------------------
    [[nodiscard]] std::string rawDeviceName(HANDLE nativeDevice);
    [[nodiscard]] bool hidDeviceInfo(HANDLE nativeDevice, RID_DEVICE_INFO &outInfo) noexcept;
    [[nodiscard]] bool hidPreparsedData(HANDLE nativeDevice, std::vector<std::byte> &outPreparsedData, HIDP_CAPS &outCaps) noexcept;
    [[nodiscard]] std::optional<Types::StandardId> standardFromHid(
        USAGE usagePage,
        USAGE usage,
        USAGE topLevelUsagePage,
        USAGE topLevelUsage) noexcept;
    [[nodiscard]] Types::NumericStorage hidNumericStorage(bool signedValue, std::uint16_t bitSize) noexcept;
    [[nodiscard]] Types::Scalar hidMinimum(LONG value, bool signedValue) noexcept;
    [[nodiscard]] Types::Scalar hidMaximum(LONG value, bool signedValue) noexcept;
    [[nodiscard]] Types::Value hidValue(const Attachment::Impl::DeviceBinding::HidElement &element, ULONG value) noexcept;
    [[nodiscard]] std::string hidElementName(USAGE usagePage, USAGE usage, std::uint32_t component);

    void addDevice(
        System::Impl &state,
        Attachment::Impl &attachment,
        DeviceRole role,
        std::uint32_t slot,
        std::string name,
        std::string manufacturer,
        bool connected,
        Types::SourceInfo source)
    {
        const std::size_t initialDeviceCount = state.devices.size();
        const std::size_t initialStateCount = state.states.size();
        const std::size_t initialBindingCount = attachment.devices.size();
        const std::size_t initialEventCount = state.pendingEvents.size();
        const std::uint32_t initialDeviceSlot = state.nextDeviceSlot;
        const std::uint32_t initialElementSlot = state.nextElementSlot;
        const std::uint64_t initialDeviceRevision = state.deviceRevision;
        const std::uint64_t initialDroppedEvents = state.pendingDroppedEvents;
        const bool initialDiscontinuity = state.pendingDiscontinuity;
        const Types::Health initialHealth = state.health;
        Types::DeviceId mergedDeviceId{};
        std::size_t initialMergedSourceCount = 0;
        std::size_t initialMergedElementCount = 0;
        bool initialMergedConnected = false;
        std::uint64_t initialMergedRevision = 0;
        Types::IdentityConfidence initialMergedConfidence = Types::IdentityConfidence::None;
        std::optional<std::uint16_t> initialMergedVendorId{};
        std::optional<std::uint16_t> initialMergedProductId{};
        bool mergedDevice = false;

        const auto rollback = [&] noexcept
        {
            state.devices.resize(initialDeviceCount);
            if(mergedDevice)
            {
                const std::size_t deviceIndex = state.findDeviceIndex(mergedDeviceId);
                if(deviceIndex != state.devices.size())
                {
                    Types::DeviceInfo &descriptor = state.devices[deviceIndex];
                    descriptor.sources.resize(initialMergedSourceCount);
                    descriptor.elements.resize(initialMergedElementCount);
                    descriptor.connected = initialMergedConnected;
                    descriptor.descriptorRevision = initialMergedRevision;
                    descriptor.identityConfidence = initialMergedConfidence;
                    descriptor.vendorId = initialMergedVendorId;
                    descriptor.productId = initialMergedProductId;
                }
            }
            state.states.resize(initialStateCount);
            attachment.devices.resize(initialBindingCount);
            state.pendingEvents.resize(initialEventCount);
            state.nextDeviceSlot = initialDeviceSlot;
            state.nextElementSlot = initialElementSlot;
            state.deviceRevision = initialDeviceRevision;
            state.pendingDroppedEvents = initialDroppedEvents;
            state.pendingDiscontinuity = initialDiscontinuity;
            state.health = initialHealth;
        };

        try
        {
            if(source.backend == Types::BackendKind::Synthetic)
            {
                source.backend = backendFor(role);
            }
            if(source.backendName.empty())
            {
                source.backendName = defaultSourceName(role);
            }
            if(source.persistentKey.empty())
            {
                source.persistentKey = deviceKey(role, slot);
            }
            if(source.identityConfidence == Types::IdentityConfidence::None && !source.persistentKey.empty())
            {
                source.identityConfidence =
                    role == DeviceRole::Gamepad ? Types::IdentityConfidence::ModelOnly : Types::IdentityConfidence::ConnectionPath;
            }
            source.active = connected;

            Types::DeviceInfo *descriptor = findLogicalDevice(state, source.persistentKey);
            const bool newLogicalDevice = descriptor == nullptr;
            const bool logicalWasConnected = descriptor != nullptr && descriptor->connected;
            if(!newLogicalDevice)
            {
                mergedDevice = true;
                mergedDeviceId = descriptor->id;
                initialMergedSourceCount = descriptor->sources.size();
                initialMergedElementCount = descriptor->elements.size();
                initialMergedConnected = descriptor->connected;
                initialMergedRevision = descriptor->descriptorRevision;
                initialMergedConfidence = descriptor->identityConfidence;
                initialMergedVendorId = descriptor->vendorId;
                initialMergedProductId = descriptor->productId;
            }
            const Types::DeviceId logicalId = newLogicalDevice ? Types::DeviceId{state.instance, state.nextDeviceSlot++, 1} : descriptor->id;
            DeviceBinding binding{
                .role = role,
                .nativeSlot = slot,
                .id = logicalId,
                .connected = connected,
                .sourceIndex = 0,
            };
            if(newLogicalDevice)
            {
                Types::DeviceInfo nextDescriptor{
                    .id = binding.id,
                    .origin = role == DeviceRole::Pointer ? Types::DeviceOrigin::OperatingSystemService : Types::DeviceOrigin::Physical,
                    .name = std::move(name),
                    .manufacturer = std::move(manufacturer),
                    .persistentKey = source.persistentKey,
                    .identityConfidence = source.identityConfidence,
                    .vendorId = source.vendorId,
                    .productId = source.productId,
                    .connected = connected,
                    .descriptorRevision = ++state.deviceRevision,
                    .sources = {source},
                };
                state.devices.push_back(std::move(nextDescriptor));
                descriptor = &state.devices.back();
            }
            else
            {
                binding.sourceIndex = descriptor->sources.size();
                descriptor->sources.push_back(std::move(source));
                const Types::SourceInfo &registeredSource = descriptor->sources.back();
                if(static_cast<unsigned int>(registeredSource.identityConfidence) > static_cast<unsigned int>(descriptor->identityConfidence))
                {
                    descriptor->identityConfidence = registeredSource.identityConfidence;
                }
                if(!descriptor->vendorId.has_value())
                {
                    descriptor->vendorId = registeredSource.vendorId;
                }
                if(!descriptor->productId.has_value())
                {
                    descriptor->productId = registeredSource.productId;
                }
                descriptor->connected = descriptor->connected || connected;
                descriptor->descriptorRevision = ++state.deviceRevision;
            }

            if(role == DeviceRole::Keyboard)
            {
                constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 3> keyboardUsageRanges = {
                    std::pair{0x04U, 0x31U},
                    std::pair{0x33U, 0x63U},
                    std::pair{0xE0U, 0xE7U}};
                for(const auto &[firstUsage, lastUsage] : keyboardUsageRanges)
                {
                    for(std::uint32_t usage = firstUsage; usage <= lastUsage; ++usage)
                    {
                        const Types::StandardId standard{Types::StandardDomain::Keyboard, usage};
                        addElement(
                            state,
                            *descriptor,
                            binding,
                            "Keyboard Usage " + std::to_string(usage),
                            standardKey(standard),
                            standard,
                            Types::ValueKind::Boolean,
                            Types::ReportingMode::Absolute,
                            {},
                            ElementRole::Standard);
                    }
                }
                const Types::StandardId application = Standard::Key::Application;
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Application/Menu",
                    standardKey(application),
                    application,
                    Types::ValueKind::Boolean,
                    Types::ReportingMode::Absolute,
                    {},
                    ElementRole::Standard);
            }
            else if(role == DeviceRole::Mouse)
            {
                const std::array<std::pair<Types::StandardId, std::string_view>, 5> buttons{
                    {{Standard::Mouse::Left, "Left Button"},
                     {Standard::Mouse::Right, "Right Button"},
                     {Standard::Mouse::Middle, "Middle Button"},
                     {Standard::Mouse::X1, "X1 Button"},
                     {Standard::Mouse::X2, "X2 Button"}}};
                for(const auto &[standard, buttonName] : buttons)
                {
                    addElement(
                        state,
                        *descriptor,
                        binding,
                        std::string(buttonName),
                        standardKey(standard),
                        standard,
                        Types::ValueKind::Boolean,
                        Types::ReportingMode::Absolute,
                        {},
                        ElementRole::Standard);
                }
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Movement",
                    "mouse:movement",
                    Standard::Mouse::Movement,
                    Types::ValueKind::Vector2I64,
                    Types::ReportingMode::Relative,
                    {component(0, "X", numericInfo(Types::NumericStorage::Signed32, {}, {}, Types::Unit::Pixel)),
                     component(1, "Y", numericInfo(Types::NumericStorage::Signed32, {}, {}, Types::Unit::Pixel))},
                    ElementRole::MouseMovement);
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Wheel",
                    "mouse:wheel",
                    Standard::Mouse::Wheel,
                    Types::ValueKind::Vector2F32,
                    Types::ReportingMode::Relative,
                    {component(0, "Vertical", numericInfo(Types::NumericStorage::Signed16, {}, {}, Types::Unit::Count)),
                     component(1, "Horizontal", numericInfo(Types::NumericStorage::Signed16, {}, {}, Types::Unit::Count))},
                    ElementRole::MouseWheel);
            }
            else if(role == DeviceRole::Pointer)
            {
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Position",
                    "pointer:position",
                    Standard::Pointer::Position,
                    Types::ValueKind::Vector2I64,
                    Types::ReportingMode::Absolute,
                    {component(0, "X", numericInfo(Types::NumericStorage::Signed32, {}, {}, Types::Unit::Pixel)),
                     component(1, "Y", numericInfo(Types::NumericStorage::Signed32, {}, {}, Types::Unit::Pixel))},
                    ElementRole::PointerPosition);
            }
            else
            {
                const std::array<std::pair<Types::StandardId, std::string_view>, 14> buttons{
                    {{Standard::Gamepad::South, "South"},
                     {Standard::Gamepad::East, "East"},
                     {Standard::Gamepad::West, "West"},
                     {Standard::Gamepad::North, "North"},
                     {Standard::Gamepad::DpadUp, "Dpad Up"},
                     {Standard::Gamepad::DpadDown, "Dpad Down"},
                     {Standard::Gamepad::DpadLeft, "Dpad Left"},
                     {Standard::Gamepad::DpadRight, "Dpad Right"},
                     {Standard::Gamepad::LeftShoulder, "Left Shoulder"},
                     {Standard::Gamepad::RightShoulder, "Right Shoulder"},
                     {Standard::Gamepad::Back, "Back"},
                     {Standard::Gamepad::Start, "Start"},
                     {Standard::Gamepad::LeftStickButton, "Left Stick Button"},
                     {Standard::Gamepad::RightStickButton, "Right Stick Button"}}};
                for(const auto &[standard, buttonName] : buttons)
                {
                    addElement(
                        state,
                        *descriptor,
                        binding,
                        std::string(buttonName),
                        standardKey(standard),
                        standard,
                        Types::ValueKind::Boolean,
                        Types::ReportingMode::Absolute,
                        {},
                        ElementRole::Standard);
                }
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Left Stick",
                    "gamepad:left-stick",
                    Standard::Gamepad::LeftStick,
                    Types::ValueKind::Vector2F32,
                    Types::ReportingMode::Absolute,
                    {component(0, "X", numericInfo(Types::NumericStorage::Signed16, -1.0F, 1.0F, Types::Unit::NormalizedByBackend)),
                     component(1, "Y", numericInfo(Types::NumericStorage::Signed16, -1.0F, 1.0F, Types::Unit::NormalizedByBackend))},
                    ElementRole::LeftStick);
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Right Stick",
                    "gamepad:right-stick",
                    Standard::Gamepad::RightStick,
                    Types::ValueKind::Vector2F32,
                    Types::ReportingMode::Absolute,
                    {component(0, "X", numericInfo(Types::NumericStorage::Signed16, -1.0F, 1.0F, Types::Unit::NormalizedByBackend)),
                     component(1, "Y", numericInfo(Types::NumericStorage::Signed16, -1.0F, 1.0F, Types::Unit::NormalizedByBackend))},
                    ElementRole::RightStick);
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Left Trigger",
                    "gamepad:left-trigger",
                    Standard::Gamepad::LeftTrigger,
                    Types::ValueKind::Float32,
                    Types::ReportingMode::Absolute,
                    {component(0, "Value", numericInfo(Types::NumericStorage::Unsigned8, 0.0F, 1.0F, Types::Unit::NormalizedByBackend))},
                    ElementRole::Trigger);
                addElement(
                    state,
                    *descriptor,
                    binding,
                    "Right Trigger",
                    "gamepad:right-trigger",
                    Standard::Gamepad::RightTrigger,
                    Types::ValueKind::Float32,
                    Types::ReportingMode::Absolute,
                    {component(0, "Value", numericInfo(Types::NumericStorage::Unsigned8, 0.0F, 1.0F, Types::Unit::NormalizedByBackend))},
                    ElementRole::Trigger);
            }

            attachment.devices.push_back(std::move(binding));
            if(newLogicalDevice && connected)
            {
                ++state.health.activeDeviceCount;
                appendDeviceEvent(
                    state,
                    Types::EventKind::DeviceConnected,
                    Types::EventCause::Reported,
                    attachment.devices.back().id,
                    descriptor->descriptorRevision);
            }
            else if(!newLogicalDevice)
            {
                if(!logicalWasConnected && connected)
                {
                    ++state.health.activeDeviceCount;
                    appendDeviceEvent(
                        state,
                        Types::EventKind::DeviceConnected,
                        Types::EventCause::Resynchronized,
                        descriptor->id,
                        descriptor->descriptorRevision);
                }
                else
                {
                    appendDeviceEvent(
                        state,
                        Types::EventKind::DeviceChanged,
                        Types::EventCause::SourceChanged,
                        descriptor->id,
                        descriptor->descriptorRevision);
                }
            }
        }
        catch(...)
        {
            rollback();
            throw;
        }
    }

    [[nodiscard]] bool addRawDevice(System::Impl &state, Attachment::Impl &attachment, DeviceRole role, HANDLE nativeDevice)
    {
        if(nativeDevice == nullptr)
        {
            return false;
        }
        if(DeviceBinding *existing = findNativeDevice(attachment, role, nativeDevice); existing != nullptr)
        {
            reconnectDevice(state, *existing);
            return true;
        }

        const std::uint32_t slot = static_cast<std::uint32_t>(attachment.devices.size() + 1);
        const bool keyboard = role == DeviceRole::Keyboard;
        const std::string label = keyboard ? "Keyboard" : "Mouse";
        const std::string path = rawDeviceName(nativeDevice);
        const std::string persistentKey = path.empty() ? std::string{} : "win32:device:path:" + path;
        const std::string displayName = path.empty() ? label : label + " (" + path + ")";

        addDevice(
            state,
            attachment,
            role,
            slot,
            displayName,
            {},
            true,
            Types::SourceInfo{
                .backend = Types::BackendKind::Win32RawInput,
                .backendName = "Win32 Raw Input",
                .active = true,
                .persistentKey = persistentKey,
                .identityConfidence = path.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
            });
        DeviceBinding &binding = attachment.devices.back();
        binding.nativeDevice = nativeDevice;
        return true;
    }

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

        const std::string nativeName = rawDeviceName(nativeDevice);
        const std::string persistentKey = nativeName.empty() ? std::string{} : "win32:device:path:" + nativeName;
        const std::size_t initialDeviceCount = state.devices.size();
        const std::size_t initialStateCount = state.states.size();
        const std::size_t initialBindingCount = attachment.devices.size();
        const std::size_t initialEventCount = state.pendingEvents.size();
        const std::uint32_t initialDeviceSlot = state.nextDeviceSlot;
        const std::uint32_t initialElementSlot = state.nextElementSlot;
        const std::uint64_t initialDeviceRevision = state.deviceRevision;
        const std::uint64_t initialDroppedEvents = state.pendingDroppedEvents;
        const bool initialDiscontinuity = state.pendingDiscontinuity;
        const Types::Health initialHealth = state.health;
        Types::DeviceId mergedDeviceId{};
        std::size_t initialMergedSourceCount = 0;
        std::size_t initialMergedElementCount = 0;
        bool initialMergedConnected = false;
        std::uint64_t initialMergedRevision = 0;
        Types::IdentityConfidence initialMergedConfidence = Types::IdentityConfidence::None;
        std::optional<std::uint16_t> initialMergedVendorId{};
        std::optional<std::uint16_t> initialMergedProductId{};
        bool mergedDevice = false;
        const auto rollback = [&] noexcept
        {
            state.devices.resize(initialDeviceCount);
            if(mergedDevice)
            {
                const std::size_t deviceIndex = state.findDeviceIndex(mergedDeviceId);
                if(deviceIndex != state.devices.size())
                {
                    Types::DeviceInfo &descriptor = state.devices[deviceIndex];
                    descriptor.sources.resize(initialMergedSourceCount);
                    descriptor.elements.resize(initialMergedElementCount);
                    descriptor.connected = initialMergedConnected;
                    descriptor.descriptorRevision = initialMergedRevision;
                    descriptor.identityConfidence = initialMergedConfidence;
                    descriptor.vendorId = initialMergedVendorId;
                    descriptor.productId = initialMergedProductId;
                }
            }
            state.states.resize(initialStateCount);
            attachment.devices.resize(initialBindingCount);
            state.pendingEvents.resize(initialEventCount);
            state.nextDeviceSlot = initialDeviceSlot;
            state.nextElementSlot = initialElementSlot;
            state.deviceRevision = initialDeviceRevision;
            state.pendingDroppedEvents = initialDroppedEvents;
            state.pendingDiscontinuity = initialDiscontinuity;
            state.health = initialHealth;
        };

        try
        {
            Types::DeviceInfo *logicalDescriptor = findLogicalDevice(state, persistentKey);
            const bool newLogicalDevice = logicalDescriptor == nullptr;
            const bool logicalWasConnected = logicalDescriptor != nullptr && logicalDescriptor->connected;
            if(!newLogicalDevice)
            {
                mergedDevice = true;
                mergedDeviceId = logicalDescriptor->id;
                initialMergedSourceCount = logicalDescriptor->sources.size();
                initialMergedElementCount = logicalDescriptor->elements.size();
                initialMergedConnected = logicalDescriptor->connected;
                initialMergedRevision = logicalDescriptor->descriptorRevision;
                initialMergedConfidence = logicalDescriptor->identityConfidence;
                initialMergedVendorId = logicalDescriptor->vendorId;
                initialMergedProductId = logicalDescriptor->productId;
            }
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
                .persistentKey = persistentKey,
                .identityConfidence = nativeName.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
                .vendorId = static_cast<std::uint16_t>(nativeInfo.hid.dwVendorId),
                .productId = static_cast<std::uint16_t>(nativeInfo.hid.dwProductId),
            };
            if(newLogicalDevice)
            {
                Types::DeviceInfo nextDescriptor{
                    .id = deviceId,
                    .origin = Types::DeviceOrigin::Physical,
                    .name = "HID Device " + std::to_string(nativeInfo.hid.usUsagePage) + ":" + std::to_string(nativeInfo.hid.usUsage),
                    .manufacturer = {},
                    .persistentKey = persistentKey,
                    .identityConfidence = source.identityConfidence,
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
                if(!logicalDescriptor->vendorId.has_value())
                {
                    logicalDescriptor->vendorId = logicalDescriptor->sources.back().vendorId;
                }
                if(!logicalDescriptor->productId.has_value())
                {
                    logicalDescriptor->productId = logicalDescriptor->sources.back().productId;
                }
            }
            Types::DeviceInfo &descriptor = *logicalDescriptor;

            constexpr std::size_t maximumElements = 4096;
            const std::size_t buttonCount = capabilities.NumberInputButtonCaps;
            const std::size_t valueCount = capabilities.NumberInputValueCaps;
            if((buttonCount == 0 && valueCount == 0) || buttonCount > maximumElements || valueCount > maximumElements ||
               buttonCount + valueCount > maximumElements)
            {
                rollback();
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
                    rollback();
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
                    rollback();
                    return false;
                }
                valueCaps.resize(count);
            }

            descriptor.elements.reserve(std::min(maximumElements, buttonCount + valueCount));
            binding.elements.reserve(std::min(maximumElements, buttonCount + valueCount));
            binding.hidElements.reserve(std::min(maximumElements, buttonCount + valueCount));

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
                    persistentKey.empty()
                        ? std::string{}
                        : persistentKey + ":control:" + std::to_string(caps.UsagePage) + ":" + std::to_string(usage) + ":" +
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
                    rollback();
                    return false;
                }
                for(std::uint32_t usage = first; usage <= last; ++usage)
                {
                    addButton(static_cast<USAGE>(usage), caps, 0);
                    ++elementCount;
                }
            }

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
                    persistentKey.empty()
                        ? std::string{}
                        : persistentKey + ":control:" + std::to_string(caps.UsagePage) + ":" + std::to_string(usage) + ":" +
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
                    rollback();
                    return false;
                }
                std::uint32_t componentIndex = 0;
                for(std::uint32_t usage = first; usage <= last; ++usage)
                {
                    addValue(static_cast<USAGE>(usage), caps, componentIndex++);
                    ++elementCount;
                }
            }

            addNormalizedGamepadElements(state, descriptor, binding, capabilities, persistentKey);

            binding.hidPreparsedData = std::move(preparsedData);
            binding.hidReportLength = capabilities.InputReportByteLength;
            const ULONG usageCapacity =
                HidP_MaxUsageListLength(HidP_Input, 0, reinterpret_cast<PHIDP_PREPARSED_DATA>(binding.hidPreparsedData.data()));
            if(usageCapacity > maximumElements)
            {
                rollback();
                return false;
            }
            binding.hidUsageScratch.resize(std::max<ULONG>(usageCapacity, 1));
            attachment.devices.push_back(std::move(binding));
            if(newLogicalDevice)
            {
                ++state.health.activeDeviceCount;
                appendDeviceEvent(state, Types::EventKind::DeviceConnected, Types::EventCause::Reported, deviceId, descriptor.descriptorRevision);
            }
            else if(!logicalWasConnected)
            {
                ++state.health.activeDeviceCount;
                appendDeviceEvent(
                    state,
                    Types::EventKind::DeviceConnected,
                    Types::EventCause::Resynchronized,
                    deviceId,
                    descriptor.descriptorRevision);
            }
            else
            {
                appendDeviceEvent(state, Types::EventKind::DeviceChanged, Types::EventCause::SourceChanged, deviceId, descriptor.descriptorRevision);
            }
            return true;
        }
        catch(const std::bad_alloc &)
        {
            rollback();
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
        const std::string nativeName = rawDeviceName(nativeDevice);
        const std::size_t initialDeviceCount = state.devices.size();
        const std::size_t initialStateCount = state.states.size();
        const std::size_t initialBindingCount = attachment.devices.size();
        const std::size_t initialEventCount = state.pendingEvents.size();
        const std::uint32_t initialDeviceSlot = state.nextDeviceSlot;
        const std::uint64_t initialDeviceRevision = state.deviceRevision;
        const std::uint64_t initialDroppedEvents = state.pendingDroppedEvents;
        const bool initialDiscontinuity = state.pendingDiscontinuity;
        const Types::Health initialHealth = state.health;
        Types::DeviceId mergedDeviceId{};
        std::size_t initialMergedSourceCount = 0;
        bool initialMergedConnected = false;
        std::uint64_t initialMergedRevision = 0;
        Types::IdentityConfidence initialMergedConfidence = Types::IdentityConfidence::None;
        std::optional<std::uint16_t> initialMergedVendorId{};
        std::optional<std::uint16_t> initialMergedProductId{};
        bool mergedDevice = false;
        const auto rollback = [&] noexcept
        {
            state.devices.resize(initialDeviceCount);
            if(mergedDevice)
            {
                const std::size_t deviceIndex = state.findDeviceIndex(mergedDeviceId);
                if(deviceIndex != state.devices.size())
                {
                    Types::DeviceInfo &descriptor = state.devices[deviceIndex];
                    descriptor.sources.resize(initialMergedSourceCount);
                    descriptor.connected = initialMergedConnected;
                    descriptor.descriptorRevision = initialMergedRevision;
                    descriptor.identityConfidence = initialMergedConfidence;
                    descriptor.vendorId = initialMergedVendorId;
                    descriptor.productId = initialMergedProductId;
                }
            }
            state.states.resize(initialStateCount);
            attachment.devices.resize(initialBindingCount);
            state.pendingEvents.resize(initialEventCount);
            state.nextDeviceSlot = initialDeviceSlot;
            state.deviceRevision = initialDeviceRevision;
            state.pendingDroppedEvents = initialDroppedEvents;
            state.pendingDiscontinuity = initialDiscontinuity;
            state.health = initialHealth;
        };

        try
        {
            const std::string persistentKey = nativeName.empty() ? std::string{} : "win32:device:path:" + nativeName;
            Types::DeviceInfo *logicalDescriptor = findLogicalDevice(state, persistentKey);
            const bool newLogicalDevice = logicalDescriptor == nullptr;
            const bool logicalWasConnected = logicalDescriptor != nullptr && logicalDescriptor->connected;
            if(!newLogicalDevice)
            {
                mergedDevice = true;
                mergedDeviceId = logicalDescriptor->id;
                initialMergedSourceCount = logicalDescriptor->sources.size();
                initialMergedConnected = logicalDescriptor->connected;
                initialMergedRevision = logicalDescriptor->descriptorRevision;
                initialMergedConfidence = logicalDescriptor->identityConfidence;
                initialMergedVendorId = logicalDescriptor->vendorId;
                initialMergedProductId = logicalDescriptor->productId;
            }
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
                hasNativeInfo ? "HID Device " + std::to_string(nativeInfo.hid.usUsagePage) + ":" + std::to_string(nativeInfo.hid.usUsage)
                              : "Opaque HID Device";
            Types::SourceInfo source{
                .backend = Types::BackendKind::Win32Hid,
                .backendName = "Win32 HID",
                .active = true,
                .persistentKey = persistentKey,
                .identityConfidence = nativeName.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
                .vendorId = hasNativeInfo ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(nativeInfo.hid.dwVendorId)} : std::nullopt,
                .productId = hasNativeInfo ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(nativeInfo.hid.dwProductId)} : std::nullopt,
            };
            if(newLogicalDevice)
            {
                Types::DeviceInfo nextDescriptor{
                    .id = deviceId,
                    .origin = Types::DeviceOrigin::Physical,
                    .name = deviceName,
                    .manufacturer = {},
                    .persistentKey = persistentKey,
                    .identityConfidence = source.identityConfidence,
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
            }
            attachment.devices.push_back(std::move(binding));
            Types::DeviceInfo &descriptor = *logicalDescriptor;
            if(newLogicalDevice)
            {
                ++state.health.activeDeviceCount;
                appendDeviceEvent(state, Types::EventKind::DeviceConnected, Types::EventCause::Reported, deviceId, descriptor.descriptorRevision);
            }
            else if(!logicalWasConnected)
            {
                ++state.health.activeDeviceCount;
                appendDeviceEvent(
                    state,
                    Types::EventKind::DeviceConnected,
                    Types::EventCause::Resynchronized,
                    deviceId,
                    descriptor.descriptorRevision);
            }
            else
            {
                appendDeviceEvent(state, Types::EventKind::DeviceChanged, Types::EventCause::SourceChanged, deviceId, descriptor.descriptorRevision);
            }
            return true;
        }
        catch(const std::bad_alloc &)
        {
            rollback();
            return false;
        }
    }

    void publishNormalizedGamepad(
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

    void publishHidReport(System::Impl &state, Attachment::Impl &attachment, DeviceBinding &device, std::span<const std::byte> report) noexcept
    {
        if(report.empty())
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }
        if(device.hidOpaque)
        {
            appendRawReport(state, state.configuration, device.id, Types::BackendKind::Win32Hid, report);
            (void)attachment;
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
        const bool hasButtons = std::any_of(
            device.hidElements.begin(),
            device.hidElements.end(),
            [](const Attachment::Impl::DeviceBinding::HidElement &element)
            {
                return element.button;
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
            if(element.reportId != 0 && std::to_integer<BYTE>(report[0]) != element.reportId)
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
        appendRawReport(state, state.configuration, device.id, Types::BackendKind::Win32Hid, report);
        (void)attachment;
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

    [[nodiscard]] bool enumerateNativeDevices(System::Impl &state, Attachment::Impl &attachment)
    {
        UINT deviceCount = 0;
        if(GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return false;
        }
        if(deviceCount == 0)
        {
            return true;
        }

        std::vector<RAWINPUTDEVICELIST> devices(static_cast<std::size_t>(deviceCount));
        UINT received = deviceCount;
        if(GetRawInputDeviceList(devices.data(), &received, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return false;
        }

        for(std::size_t index = 0; index < static_cast<std::size_t>(received); ++index)
        {
            const RAWINPUTDEVICELIST &device = devices[index];
            if(device.dwType == RIM_TYPEKEYBOARD)
            {
                (void)addRawDevice(state, attachment, DeviceRole::Keyboard, device.hDevice);
                continue;
            }
            if(device.dwType == RIM_TYPEMOUSE)
            {
                (void)addRawDevice(state, attachment, DeviceRole::Mouse, device.hDevice);
                continue;
            }
            if(device.dwType != RIM_TYPEHID)
            {
                continue;
            }

            if(!addHidDevice(state, attachment, device.hDevice))
            {
                (void)addOpaqueHidDevice(state, attachment, device.hDevice);
            }
        }
        return true;
    }

    // ------------------------------------------------------------
    // Native device metadata and attachment cleanup
    // ------------------------------------------------------------
    [[nodiscard]] std::string rawDeviceName(HANDLE nativeDevice)
    {
        UINT characterCount = 0;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_DEVICENAME, nullptr, &characterCount) == static_cast<UINT>(-1) || characterCount == 0)
        {
            return {};
        }

        std::vector<wchar_t> buffer(static_cast<std::size_t>(characterCount) + 1, L'\0');
        UINT received = characterCount;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_DEVICENAME, buffer.data(), &received) == static_cast<UINT>(-1) || received == 0 ||
           received > buffer.size())
        {
            return {};
        }
        while(received != 0 && buffer[received - 1] == L'\0')
        {
            --received;
        }
        if(received == 0)
        {
            return {};
        }

        std::u16string codeUnits;
        codeUnits.reserve(received);
        for(const wchar_t value : std::span<const wchar_t>(buffer).first(received))
        {
            codeUnits.push_back(static_cast<char16_t>(value));
        }
        const auto measured = GameWIP::Unicode::Utf16::measureToUtf8(codeUnits);
        if(measured.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            return {};
        }
        std::string result(measured.requiredBytes, '\0');
        const auto converted = GameWIP::Unicode::Utf16::convertToUtf8(codeUnits, std::span<char>(result.data(), result.size()));
        if(converted.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            return {};
        }
        return result;
    }

    [[nodiscard]] bool hidDeviceInfo(HANDLE nativeDevice, RID_DEVICE_INFO &outInfo) noexcept
    {
        outInfo = {};
        outInfo.cbSize = sizeof(outInfo);
        UINT size = sizeof(outInfo);
        return GetRawInputDeviceInfoW(nativeDevice, RIDI_DEVICEINFO, &outInfo, &size) != static_cast<UINT>(-1) && outInfo.dwType == RIM_TYPEHID;
    }

    [[nodiscard]] bool hidPreparsedData(HANDLE nativeDevice, std::vector<std::byte> &outPreparsedData, HIDP_CAPS &outCaps) noexcept
    {
        UINT size = 0;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_PREPARSEDDATA, nullptr, &size) == static_cast<UINT>(-1) || size == 0)
        {
            return false;
        }
        try
        {
            outPreparsedData.resize(size);
        }
        catch(const std::bad_alloc &)
        {
            return false;
        }

        UINT received = size;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_PREPARSEDDATA, outPreparsedData.data(), &received) == static_cast<UINT>(-1) || received != size)
        {
            outPreparsedData.clear();
            return false;
        }

        outCaps = {};
        const auto preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(outPreparsedData.data());
        return HidP_GetCaps(preparsed, &outCaps) == HIDP_STATUS_SUCCESS;
    }

    [[nodiscard]] std::optional<Types::StandardId> standardFromHid(
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

    [[nodiscard]] Types::NumericStorage hidNumericStorage(bool signedValue, std::uint16_t bitSize) noexcept
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

    [[nodiscard]] Types::Scalar hidMinimum(LONG value, bool signedValue) noexcept
    {
        return signedValue ? Types::Scalar{static_cast<std::int64_t>(value)} : Types::Scalar{static_cast<std::uint64_t>(static_cast<ULONG>(value))};
    }

    [[nodiscard]] Types::Scalar hidMaximum(LONG value, bool signedValue) noexcept
    {
        return signedValue ? Types::Scalar{static_cast<std::int64_t>(value)} : Types::Scalar{static_cast<std::uint64_t>(static_cast<ULONG>(value))};
    }

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

    [[nodiscard]] Types::Value hidValue(const Attachment::Impl::DeviceBinding::HidElement &element, ULONG value) noexcept
    {
        if(element.signedValue)
        {
            return signExtendHidValue(value, element.bitSize);
        }
        return static_cast<std::uint64_t>(value);
    }

    [[nodiscard]] std::string hidElementName(USAGE usagePage, USAGE usage, std::uint32_t component)
    {
        return "HID Usage " + std::to_string(usagePage) + ":" + std::to_string(usage) + "." + std::to_string(component);
    }

    void unregisterRawInput(Attachment::Impl &attachment) noexcept
    {
        for(const auto [usagePage, usage] : attachment.rawInputCollections)
        {
            RAWINPUTDEVICE device{
                .usUsagePage = usagePage,
                .usUsage = usage,
                .dwFlags = RIDEV_REMOVE,
                .hwndTarget = nullptr,
            };
            (void)RegisterRawInputDevices(&device, 1, sizeof(device));
        }
        attachment.rawInputCollections.clear();
    }

    [[nodiscard]] IO::Types::Status checkAttached(const Attachment::Impl &attachment) noexcept
    {
        if(!attachment.attached || attachment.system == nullptr)
        {
            return status(ErrorCode::NotOpen);
        }
        if(!attachment.system->isOpen())
        {
            return status(ErrorCode::NotOpen);
        }
        return attachment.system->ownedByCurrentThread() ? IO::successStatus() : status(ErrorCode::ResourceBusy);
    }
} // namespace GameWIP::Input::Platform::Win32::Detail
