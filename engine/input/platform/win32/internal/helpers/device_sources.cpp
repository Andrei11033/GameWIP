/// @file device_sources.cpp
/// @brief Shared Win32 device and element registration.

#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"
#include "input/platform/win32/internal/device_registration.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Device source metadata
    /// ------------------------------------------------------------

    [[nodiscard]] static Types::BackendKind backendFor(DeviceRole role) noexcept
    {
        switch(role)
        {
        case DeviceRole::Gamepad:
            return Types::BackendKind::XInput;
        case DeviceRole::Hid:
            return Types::BackendKind::Win32Hid;
        case DeviceRole::Pointer:
        case DeviceRole::MessageKeyboard:
            return Types::BackendKind::Win32WindowMessage;
        case DeviceRole::GameInputController:
        case DeviceRole::GameInputGamepad:
            return Types::BackendKind::GameInput;
        case DeviceRole::DirectInputKeyboard:
        case DeviceRole::DirectInputMouse:
        case DeviceRole::DirectInputController:
            return Types::BackendKind::DirectInput;
        case DeviceRole::Keyboard:
        case DeviceRole::Mouse:
            return Types::BackendKind::Win32RawInput;
        }

        return Types::BackendKind::Win32RawInput;
    }

    [[nodiscard]] static std::string defaultSourceName(DeviceRole role)
    {
        switch(role)
        {
        case DeviceRole::Gamepad:
            return "XInput";
        case DeviceRole::Pointer:
        case DeviceRole::MessageKeyboard:
            return "Win32 Window Messages";
        case DeviceRole::GameInputController:
        case DeviceRole::GameInputGamepad:
            return "Microsoft GameInput";
        case DeviceRole::DirectInputKeyboard:
        case DeviceRole::DirectInputMouse:
        case DeviceRole::DirectInputController:
            return "Microsoft DirectInput";
        case DeviceRole::Hid:
            return "Win32 HID";
        case DeviceRole::Keyboard:
        case DeviceRole::Mouse:
            return "Win32 Raw Input";
        }

        return "Win32 Raw Input";
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

    /// ------------------------------------------------------------
    /// Standard control registration
    /// ------------------------------------------------------------

    static void addKeyboardElements(SystemState &state, Types::DeviceInfo &device, DeviceBinding &binding)
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
                    device,
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
            device,
            binding,
            "Application/Menu",
            standardKey(application),
            application,
            Types::ValueKind::Boolean,
            Types::ReportingMode::Absolute,
            {},
            ElementRole::Standard);
    }

    static void addMouseElements(SystemState &state, Types::DeviceInfo &device, DeviceBinding &binding)
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
                device,
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
            device,
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
            device,
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

    static void addPointerElements(SystemState &state, Types::DeviceInfo &device, DeviceBinding &binding)
    {
        addElement(
            state,
            device,
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

    static void addGamepadElements(SystemState &state, Types::DeviceInfo &device, DeviceBinding &binding)
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
                device,
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
            device,
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
            device,
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
            device,
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
            device,
            binding,
            "Right Trigger",
            "gamepad:right-trigger",
            Standard::Gamepad::RightTrigger,
            Types::ValueKind::Float32,
            Types::ReportingMode::Absolute,
            {component(0, "Value", numericInfo(Types::NumericStorage::Unsigned8, 0.0F, 1.0F, Types::Unit::NormalizedByBackend))},
            ElementRole::Trigger);
    }

    /// ------------------------------------------------------------
    /// Element and device registration
    /// ------------------------------------------------------------

    Types::ElementId addElement(
        SystemState &state,
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
            .reporting = reporting,
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
        SystemState &state,
        AttachmentState &attachment,
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

            // Add the controls reported by this backend.

            if(role == DeviceRole::Keyboard || role == DeviceRole::MessageKeyboard || role == DeviceRole::DirectInputKeyboard)
            {
                addKeyboardElements(state, *descriptor, binding);
            }

            else if(role == DeviceRole::Mouse || role == DeviceRole::DirectInputMouse)
            {
                addMouseElements(state, *descriptor, binding);
            }

            else if(role == DeviceRole::Pointer)
            {
                addPointerElements(state, *descriptor, binding);
            }

            else if(role == DeviceRole::Gamepad || role == DeviceRole::GameInputGamepad)
            {
                addGamepadElements(state, *descriptor, binding);
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
} // namespace GameWIP::Input::Platform::Win32
