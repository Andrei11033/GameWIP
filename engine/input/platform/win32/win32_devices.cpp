/// @file win32_devices.cpp
/// @brief Win32 device source registration and enumeration.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"
#include "input/platform/win32/internal/device_registration.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// ------------------------------------------------------------
    /// Shared identity and descriptor helpers
    /// ------------------------------------------------------------

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

        if(role == DeviceRole::Pointer || role == DeviceRole::MessageKeyboard)
        {
            return Types::BackendKind::Win32WindowMessage;
        }

        if(role == DeviceRole::GameInputController || role == DeviceRole::GameInputGamepad)
        {
            return Types::BackendKind::GameInput;
        }

        if(role == DeviceRole::DirectInputKeyboard || role == DeviceRole::DirectInputMouse || role == DeviceRole::DirectInputController)
        {
            return Types::BackendKind::DirectInput;
        }

        return Types::BackendKind::Win32RawInput;
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

        if(role == DeviceRole::MessageKeyboard)
        {
            return "Win32 Window Messages";
        }

        if(role == DeviceRole::GameInputController || role == DeviceRole::GameInputGamepad)
        {
            return "Microsoft GameInput";
        }

        if(role == DeviceRole::DirectInputKeyboard || role == DeviceRole::DirectInputMouse || role == DeviceRole::DirectInputController)
        {
            return "Microsoft DirectInput";
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
        case DeviceRole::MessageKeyboard:
            return {};
        case DeviceRole::Gamepad:
            return "xinput:slot:" + std::to_string(slot);
        case DeviceRole::Hid:
        case DeviceRole::GameInputController:
        case DeviceRole::GameInputGamepad:
        case DeviceRole::DirectInputKeyboard:
        case DeviceRole::DirectInputMouse:
        case DeviceRole::DirectInputController:
            return {};
        }

        return "win32:device";
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

    /// ------------------------------------------------------------
    /// Device source and descriptor lifecycle
    /// ------------------------------------------------------------

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
        std::string sourcePrefix;
        if(binding.sourceIndex < device.sources.size() && !device.sources[binding.sourceIndex].persistentKey.empty())
        {
            sourcePrefix = device.sources[binding.sourceIndex].persistentKey + ":";
            if(!key.starts_with(sourcePrefix))
            {
                key.insert(0, sourcePrefix);
            }
        }

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
                [&identity, &sourcePrefix, standard, bindingRole](const Types::ElementInfo &candidate)
                {
                    const bool sameSource = sourcePrefix.empty() || candidate.persistentKey.starts_with(sourcePrefix);
                    if(!sameSource)
                    {
                        return false;
                    }

                    if(candidate.identity == identity)
                    {
                        return true;
                    }

                    // Raw keyboard/mouse and HID collections can describe
                    // the same standard control with different collection
                    // metadata. Share that canonical element only when both
                    // reports come from the same native interface.
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
            .provenance = (binding.role == DeviceRole::Gamepad || binding.role == DeviceRole::GameInputController ||
                           binding.role == DeviceRole::GameInputGamepad || binding.role == DeviceRole::DirectInputKeyboard ||
                           binding.role == DeviceRole::DirectInputMouse || binding.role == DeviceRole::DirectInputController)
                              ? Types::ValueProvenance::NativeApiProcessed
                              : Types::ValueProvenance::NativeDecoded,
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

    void addDevice(
        System::Impl &state,
        Attachment::Impl &attachment,
        DeviceRole role,
        std::uint32_t slot,
        std::string name,
        std::string manufacturer,
        bool connected,
        Types::SourceInfo source,
        std::string_view logicalPersistentKey,
        Types::IdentityConfidence logicalIdentityConfidence)
    {
        DeviceRegistrationRollback transaction{state, attachment};

        try
        {
            // Fill in source metadata needed for grouping.
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

            // Resolve the identity used to group backend sources.
            const std::string_view deviceIdentity = logicalPersistentKey.empty() ? std::string_view{source.persistentKey} : logicalPersistentKey;
            const Types::IdentityConfidence deviceIdentityConfidence =
                logicalIdentityConfidence == Types::IdentityConfidence::None ? source.identityConfidence : logicalIdentityConfidence;

            // Find or create the shared logical device.
            Types::DeviceInfo *descriptor = findLogicalDevice(state, deviceIdentity);
            const bool newLogicalDevice = descriptor == nullptr;
            const bool logicalWasConnected = descriptor != nullptr && descriptor->connected;

            if(!newLogicalDevice)
            {
                transaction.saveMergedDevice(*descriptor);
            }

            // Create or update the logical descriptor and source binding.
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
                    .origin = (role == DeviceRole::Pointer || role == DeviceRole::MessageKeyboard) ? Types::DeviceOrigin::OperatingSystemService
                                                                                                   : Types::DeviceOrigin::Physical,
                    .name = std::move(name),
                    .manufacturer = std::move(manufacturer),
                    .persistentKey = std::string(deviceIdentity),
                    .identityConfidence = deviceIdentityConfidence,
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

                if(static_cast<unsigned int>(deviceIdentityConfidence) > static_cast<unsigned int>(descriptor->identityConfidence))
                {
                    descriptor->identityConfidence = deviceIdentityConfidence;
                }

                if(!descriptor->vendorId.has_value())
                {
                    descriptor->vendorId = registeredSource.vendorId;
                }

                if(!descriptor->productId.has_value())
                {
                    descriptor->productId = registeredSource.productId;
                }

                if(isGenericDeviceName(descriptor->name) && !isGenericDeviceName(name))
                {
                    descriptor->name = std::move(name);
                }

                if(descriptor->manufacturer.empty() && !manufacturer.empty())
                {
                    descriptor->manufacturer = std::move(manufacturer);
                }
                descriptor->connected = descriptor->connected || connected;
                descriptor->descriptorRevision = ++state.deviceRevision;
            }

            // Add keyboard controls.
            if(role == DeviceRole::Keyboard || role == DeviceRole::MessageKeyboard || role == DeviceRole::DirectInputKeyboard)
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
            // Add mouse buttons, movement, and wheel controls.
            else if(role == DeviceRole::Mouse || role == DeviceRole::DirectInputMouse)
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
            // Add the window-level cursor position control.
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
            // Add standard gamepad controls.
            else if(role == DeviceRole::Gamepad || role == DeviceRole::GameInputGamepad)
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

            // Attach the source binding and publish the device change.
            attachment.devices.push_back(std::move(binding));
            publishDeviceRegistration(state, *descriptor, newLogicalDevice, logicalWasConnected, connected);
        }
        catch(...)
        {
            transaction.restore();
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
        std::wstring nativePath;
        const std::string path = rawDeviceName(nativeDevice, &nativePath);
        const std::string pathKey = path.empty() ? std::string{} : "win32:device:path:" + path;
        const std::string containerKey = containerPersistentKey(nativePath);
        const std::string logicalKey = containerKey.empty() ? pathKey : containerKey;
        const Types::IdentityConfidence logicalConfidence =
            pathKey.empty() ? Types::IdentityConfidence::None
                            : (containerKey.empty() ? Types::IdentityConfidence::ConnectionPath : Types::IdentityConfidence::DeviceUnique);
        const HidDeviceStrings deviceStrings = queryHidDeviceStrings(nativePath);
        const std::string displayName = deviceStrings.product.empty() ? label : deviceStrings.product;

        addDevice(
            state,
            attachment,
            role,
            slot,
            displayName,
            deviceStrings.manufacturer,
            true,
            Types::SourceInfo{
                .backend = Types::BackendKind::Win32RawInput,
                .backendName = "Win32 Raw Input",
                .active = true,
                .persistentKey = pathKey,
                .identityConfidence = pathKey.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
            },
            logicalKey,
            logicalConfidence);
        DeviceBinding &binding = attachment.devices.back();
        binding.nativeDevice = nativeDevice;
        return true;
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
                if(attachment.rawInputEnabled)
                {
                    (void)addRawDevice(state, attachment, DeviceRole::Keyboard, device.hDevice);
                }
                continue;
            }

            if(device.dwType == RIM_TYPEMOUSE)
            {
                if(attachment.rawInputEnabled)
                {
                    (void)addRawDevice(state, attachment, DeviceRole::Mouse, device.hDevice);
                }
                continue;
            }

            if(device.dwType != RIM_TYPEHID || !attachment.hidEnabled)
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

    /// ------------------------------------------------------------
    /// Raw Input cleanup and session validation
    /// ------------------------------------------------------------

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
