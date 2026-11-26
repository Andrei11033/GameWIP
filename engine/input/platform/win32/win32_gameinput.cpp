/// @file win32_gameinput.cpp
/// @brief Windows GameInput controller discovery and polling.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"
#include "input/platform/win32/internal/win32_unicode.h"
#include "input/platform/win32/internal/win32_keyboard_mapping.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>

#include <GameInput.h>

namespace GameWIP::Input::Platform::Win32
{
    namespace GameInputApi = ::GameInput::v1;

    namespace
    {
        constexpr std::size_t kMaximumAxes = 64;
        constexpr std::size_t kMaximumButtons = 256;
        constexpr std::size_t kMaximumSwitches = 32;

        template <typename Interface> struct InterfaceReleaser
        {
            void operator()(Interface *value) const noexcept
            {
                if(value != nullptr)
                {
                    value->Release();
                }
            }
        };

        template <typename Interface> using InterfaceReference = std::unique_ptr<Interface, InterfaceReleaser<Interface>>;

        struct ModuleReleaser
        {
            void operator()(std::remove_pointer_t<HMODULE> *module) const noexcept
            {
                if(module != nullptr)
                {
                    FreeLibrary(module);
                }
            }
        };

        using ModuleReference = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleReleaser>;

        using GameInputDeviceReference = InterfaceReference<GameInputApi::IGameInputDevice>;
        using GameInputReadingReference = InterfaceReference<GameInputApi::IGameInputReading>;

        using GameInputInitializeFunction = HRESULT(WINAPI *)(REFIID interfaceId, void **instance);
        using GameInputCreateFunction = HRESULT(WINAPI *)(void **legacy);

        /// ------------------------------------------------------------
        /// Device metadata helpers
        /// ------------------------------------------------------------

        [[nodiscard]] bool hasInputKind(GameInputApi::GameInputKind inputKinds, GameInputApi::GameInputKind kind) noexcept
        {
            return (static_cast<std::uint32_t>(inputKinds) & static_cast<std::uint32_t>(kind)) != 0;
        }

        [[nodiscard]] bool isControllerInput(GameInputApi::GameInputKind inputKinds) noexcept
        {
            constexpr std::uint32_t kControllerKinds = static_cast<std::uint32_t>(GameInputApi::GameInputKindController) |
                                                       static_cast<std::uint32_t>(GameInputApi::GameInputKindGamepad) |
                                                       static_cast<std::uint32_t>(GameInputApi::GameInputKindArcadeStick) |
                                                       static_cast<std::uint32_t>(GameInputApi::GameInputKindFlightStick) |
                                                       static_cast<std::uint32_t>(GameInputApi::GameInputKindRacingWheel) |
                                                       static_cast<std::uint32_t>(GameInputApi::GameInputKindUiNavigation);
            return (static_cast<std::uint32_t>(inputKinds) & kControllerKinds) != 0;
        }

        /// ------------------------------------------------------------
        /// Controller descriptors
        /// ------------------------------------------------------------

        void appendGameInputElements(
            SystemState &state,
            Types::DeviceInfo &descriptor,
            DeviceBinding &binding,
            const GameInputApi::GameInputDeviceInfo &info)
        {
            const std::size_t axisCount = std::min<std::size_t>(info.controllerAxisCount, kMaximumAxes);
            const std::size_t buttonCount = std::min<std::size_t>(info.controllerButtonCount, kMaximumButtons);
            const std::size_t switchCount = std::min<std::size_t>(info.controllerSwitchCount, kMaximumSwitches);

            for(std::size_t index = 0; index < axisCount; ++index)
            {
                addElement(
                    state,
                    descriptor,
                    binding,
                    std::format("Axis {}", index + 1),
                    std::format("gameinput:axis:{}", index),
                    std::nullopt,
                    Types::ValueKind::Float32,
                    Types::ReportingMode::Absolute,
                    {Types::ComponentInfo{
                        .index = 0,
                        .name = "Value",
                        .numeric =
                            Types::NumericInfo{
                                .nativeStorage = Types::NumericStorage::Float32,
                                .minimum = -1.0F,
                                .maximum = 1.0F,
                                .unit = Types::Unit::NormalizedByBackend,
                            },
                    }},
                    ElementRole::NativeAxis);
                binding.elements.back().nativeIndex = static_cast<std::uint32_t>(index);
            }

            for(std::size_t index = 0; index < buttonCount; ++index)
            {
                addElement(
                    state,
                    descriptor,
                    binding,
                    std::format("Button {}", index + 1),
                    std::format("gameinput:button:{}", index),
                    std::nullopt,
                    Types::ValueKind::Boolean,
                    Types::ReportingMode::Absolute,
                    {},
                    ElementRole::NativeButton);
                binding.elements.back().nativeIndex = static_cast<std::uint32_t>(index);
            }

            for(std::size_t index = 0; index < switchCount; ++index)
            {
                addElement(
                    state,
                    descriptor,
                    binding,
                    std::format("Switch {}", index + 1),
                    std::format("gameinput:switch:{}", index),
                    std::nullopt,
                    Types::ValueKind::UnsignedInteger,
                    Types::ReportingMode::Absolute,
                    {Types::ComponentInfo{
                        .index = 0,
                        .name = "Position",
                        .numeric =
                            Types::NumericInfo{
                                .nativeStorage = Types::NumericStorage::Unsigned8,
                                .minimum = std::uint64_t{0},
                                .maximum = std::uint64_t{8},
                            },
                    }},
                    ElementRole::NativeSwitch);
                binding.elements.back().nativeIndex = static_cast<std::uint32_t>(index);
            }
        }

        /// ------------------------------------------------------------
        /// Device lifecycle and callbacks
        /// ------------------------------------------------------------

        void addGameInputSource(
            SystemState &state,
            AttachmentState &attachment,
            GameInputApi::IGameInputDevice *nativeDevice,
            const GameInputApi::GameInputDeviceInfo &info,
            DeviceRole role,
            std::string_view name,
            std::string_view sourceSuffix)
        {
            const std::uint32_t slot = static_cast<std::uint32_t>(attachment.devices.size() + 1);

            // Resolve a connection path for persistent source identity and device grouping.
            std::string persistentKey;
            std::string logicalKey;
            Types::IdentityConfidence logicalConfidence = Types::IdentityConfidence::None;

            if(info.pnpPath != nullptr && info.pnpPath[0] != '\0')
            {
                const std::string path(info.pnpPath);
                persistentKey = "win32:device:path:" + path;
                if(!sourceSuffix.empty())
                {
                    persistentKey += sourceSuffix;
                }
                const std::wstring widePath = utf8ToWide(path);
                const std::string containerKey = containerPersistentKey(widePath);
                logicalKey = containerKey.empty() ? persistentKey : containerKey;
                logicalConfidence = containerKey.empty() ? Types::IdentityConfidence::ConnectionPath : Types::IdentityConfidence::DeviceUnique;
            }

            // Register the source, then add the provider's controller elements.
            Types::SourceInfo source{
                .backend = Types::BackendKind::GameInput,
                .backendName = "Microsoft GameInput",
                .active = true,
                .persistentKey = persistentKey,
                .identityConfidence = persistentKey.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
                .vendorId = info.vendorId == 0 ? std::nullopt : std::optional<std::uint16_t>{info.vendorId},
                .productId = info.productId == 0 ? std::nullopt : std::optional<std::uint16_t>{info.productId},
            };

            addDevice(state, attachment, role, slot, std::string(name), {}, true, std::move(source), logicalKey, logicalConfidence);

            DeviceBinding &binding = attachment.devices.back();
            binding.nativeDevice = nativeDevice;
            binding.nativeKind = static_cast<std::uint32_t>(info.supportedInput);
            nativeDevice->AddRef();

            if(role == DeviceRole::GameInputController || role == DeviceRole::GameInputGamepad)
            {
                const std::size_t descriptorIndex = state.findDeviceIndex(binding.id);
                if(descriptorIndex != state.devices.size())
                {
                    appendGameInputElements(state, state.devices[descriptorIndex], binding, info);
                }
            }
        }

        void registerGameInputDevice(SystemState &state, AttachmentState &attachment, GameInputApi::IGameInputDevice *nativeDevice)
        {
            const GameInputApi::GameInputDeviceInfo *info = nullptr;
            if(FAILED(nativeDevice->GetDeviceInfo(&info)) || info == nullptr || !isControllerInput(info->supportedInput))
            {
                return;
            }

            const std::string name = info->displayName == nullptr || info->displayName[0] == '\0' ? "GameInput Controller" : info->displayName;
            const bool gamepad = info->gamepadInfo != nullptr || hasInputKind(info->supportedInput, GameInputApi::GameInputKindGamepad);
            const DeviceRole role = gamepad ? DeviceRole::GameInputGamepad : DeviceRole::GameInputController;

            if(DeviceBinding *existing = findNativeDevice(attachment, role, nativeDevice); existing != nullptr)
            {
                reconnectDevice(state, *existing);
                return;
            }

            addGameInputSource(state, attachment, nativeDevice, *info, role, name, {});
        }

        void disconnectGameInputDevice(SystemState &state, AttachmentState &attachment, GameInputApi::IGameInputDevice *nativeDevice)
        {
            for(DeviceBinding &binding : attachment.devices)
            {
                if((binding.role == DeviceRole::GameInputController || binding.role == DeviceRole::GameInputGamepad) &&
                   binding.nativeDevice == nativeDevice)
                {
                    invalidateDevice(state, attachment, binding, Types::EventCause::Disconnected);
                }
            }
        }

        void CALLBACK gameInputDeviceCallback(
            GameInputApi::GameInputCallbackToken,
            void *context,
            GameInputApi::IGameInputDevice *device,
            std::uint64_t,
            GameInputApi::GameInputDeviceStatus currentStatus,
            GameInputApi::GameInputDeviceStatus previousStatus)
        {
            auto *attachment = static_cast<AttachmentState *>(context);
            if(attachment == nullptr || device == nullptr)
            {
                return;
            }

            AcquireSRWLockExclusive(&attachment->gameInputEventLock);
            if(attachment->gameInputEventCount == attachment->gameInputEvents.size())
            {
                attachment->gameInputEventOverflow = true;
                ReleaseSRWLockExclusive(&attachment->gameInputEventLock);
                return;
            }

            const std::size_t tail = (attachment->gameInputEventHead + attachment->gameInputEventCount) % attachment->gameInputEvents.size();
            device->AddRef();
            GameInputDeviceReference retainedDevice(device);
            attachment->gameInputEvents[tail] = {
                .device = retainedDevice.release(),
                .currentStatus = static_cast<std::uint32_t>(currentStatus),
                .previousStatus = static_cast<std::uint32_t>(previousStatus),
            };
            ++attachment->gameInputEventCount;
            ReleaseSRWLockExclusive(&attachment->gameInputEventLock);
        }

        /// ------------------------------------------------------------
        /// Input state publication
        /// ------------------------------------------------------------

        [[nodiscard]] bool gamepadButton(GameInputApi::GameInputGamepadButtons buttons, GameInputApi::GameInputGamepadButtons mask) noexcept
        {
            return (static_cast<std::uint32_t>(buttons) & static_cast<std::uint32_t>(mask)) != 0;
        }

        void publishGameInputController(SystemState &state, DeviceBinding &binding, GameInputApi::IGameInputReading &reading)
        {
            // Read generic axes.
            std::array<float, kMaximumAxes> axes{};
            const std::uint32_t axisCount = std::min<std::uint32_t>(reading.GetControllerAxisCount(), static_cast<std::uint32_t>(axes.size()));
            if(axisCount != 0)
            {
                const std::uint32_t readCount = std::min(reading.GetControllerAxisState(axisCount, axes.data()), axisCount);
                for(const ElementBinding &element : binding.elements)
                {
                    if(element.role == ElementRole::NativeAxis && element.nativeIndex < readCount && std::isfinite(axes[element.nativeIndex]))
                    {
                        publish(state, element.id, axes[element.nativeIndex], false);
                    }
                }
            }

            // Read generic buttons.
            std::array<bool, kMaximumButtons> buttons{};
            const std::uint32_t buttonCount = std::min<std::uint32_t>(reading.GetControllerButtonCount(), static_cast<std::uint32_t>(buttons.size()));
            if(buttonCount != 0)
            {
                const std::uint32_t readCount = std::min(reading.GetControllerButtonState(buttonCount, buttons.data()), buttonCount);
                for(const ElementBinding &element : binding.elements)
                {
                    if(element.role == ElementRole::NativeButton && element.nativeIndex < readCount)
                    {
                        publish(state, element.id, buttons[element.nativeIndex], false);
                    }
                }
            }

            // Read generic switch positions.
            std::array<GameInputApi::GameInputSwitchPosition, kMaximumSwitches> switches{};
            const std::uint32_t switchCount =
                std::min<std::uint32_t>(reading.GetControllerSwitchCount(), static_cast<std::uint32_t>(switches.size()));
            if(switchCount != 0)
            {
                const std::uint32_t readCount = std::min(reading.GetControllerSwitchState(switchCount, switches.data()), switchCount);
                for(const ElementBinding &element : binding.elements)
                {
                    if(element.role == ElementRole::NativeSwitch && element.nativeIndex < readCount &&
                       switches[element.nativeIndex] >= GameInputApi::GameInputSwitchCenter &&
                       switches[element.nativeIndex] <= GameInputApi::GameInputSwitchUpLeft)
                    {
                        publish(state, element.id, static_cast<std::uint64_t>(switches[element.nativeIndex]), false);
                    }
                }
            }
        }

        void publishGameInputGamepad(SystemState &state, DeviceBinding &binding, GameInputApi::IGameInputReading &reading)
        {
            GameInputApi::GameInputGamepadState gamepad{};
            if(!reading.GetGamepadState(&gamepad))
            {
                return;
            }

            // Map the standardized gamepad buttons.
            constexpr std::array<std::pair<GameInputApi::GameInputGamepadButtons, Types::StandardId>, 14> buttonMap{{
                {GameInputApi::GameInputGamepadA, Standard::Gamepad::South},
                {GameInputApi::GameInputGamepadB, Standard::Gamepad::East},
                {GameInputApi::GameInputGamepadX, Standard::Gamepad::West},
                {GameInputApi::GameInputGamepadY, Standard::Gamepad::North},
                {GameInputApi::GameInputGamepadDPadUp, Standard::Gamepad::DpadUp},
                {GameInputApi::GameInputGamepadDPadDown, Standard::Gamepad::DpadDown},
                {GameInputApi::GameInputGamepadDPadLeft, Standard::Gamepad::DpadLeft},
                {GameInputApi::GameInputGamepadDPadRight, Standard::Gamepad::DpadRight},
                {GameInputApi::GameInputGamepadLeftShoulder, Standard::Gamepad::LeftShoulder},
                {GameInputApi::GameInputGamepadRightShoulder, Standard::Gamepad::RightShoulder},
                {GameInputApi::GameInputGamepadMenu, Standard::Gamepad::Start},
                {GameInputApi::GameInputGamepadView, Standard::Gamepad::Back},
                {GameInputApi::GameInputGamepadLeftThumbstick, Standard::Gamepad::LeftStickButton},
                {GameInputApi::GameInputGamepadRightThumbstick, Standard::Gamepad::RightStickButton},
            }};

            for(const auto &[mask, standard] : buttonMap)
            {
                if(ElementBinding *button = standardElement(binding, standard); button != nullptr)
                {
                    publish(state, button->id, gamepadButton(gamepad.buttons, mask), false);
                }
            }

            // Publish paired sticks and normalized triggers.
            if(ElementBinding *leftStick = findRole(binding, ElementRole::LeftStick); leftStick != nullptr)
            {
                publish(state, leftStick->id, Types::Vector2F32{gamepad.leftThumbstickX, gamepad.leftThumbstickY}, false);
            }

            if(ElementBinding *rightStick = findRole(binding, ElementRole::RightStick); rightStick != nullptr)
            {
                publish(state, rightStick->id, Types::Vector2F32{gamepad.rightThumbstickX, gamepad.rightThumbstickY}, false);
            }

            if(ElementBinding *leftTrigger = standardElement(binding, Standard::Gamepad::LeftTrigger); leftTrigger != nullptr)
            {
                publish(state, leftTrigger->id, std::clamp(gamepad.leftTrigger, 0.0F, 1.0F), false);
            }

            if(ElementBinding *rightTrigger = standardElement(binding, Standard::Gamepad::RightTrigger); rightTrigger != nullptr)
            {
                publish(state, rightTrigger->id, std::clamp(gamepad.rightTrigger, 0.0F, 1.0F), false);
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Provider lifecycle
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status initializeGameInput(AttachmentState &attachment, SystemState &state) noexcept
    {
        // Load GameInput from the system directory and resolve a supported entry point.
        HMODULE module = LoadLibraryExW(L"GameInput.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(module == nullptr)
        {
            return status(GameWIP::Status::Types::Code::Unsupported);
        }
        ModuleReference moduleReference(module);

        GameInputApi::IGameInput *nativeGameInput = nullptr;
        HRESULT result = E_NOTIMPL;

        static_assert(sizeof(GameInputInitializeFunction) == sizeof(FARPROC));
        const GameInputInitializeFunction initialize = std::bit_cast<GameInputInitializeFunction>(GetProcAddress(module, "GameInputInitialize"));
        if(initialize != nullptr)
        {
            result = initialize(GameInputApi::IID_IGameInput, reinterpret_cast<void **>(&nativeGameInput));
        }
        else
        {
            static_assert(sizeof(GameInputCreateFunction) == sizeof(FARPROC));
            const GameInputCreateFunction create = std::bit_cast<GameInputCreateFunction>(GetProcAddress(module, "GameInputCreate"));
            if(create == nullptr)
            {
                return status(GameWIP::Status::Types::Code::Unsupported);
            }

            IUnknown *nativeLegacy = nullptr;
            result = create(reinterpret_cast<void **>(&nativeLegacy));
            InterfaceReference<IUnknown> legacy(nativeLegacy);
            if(SUCCEEDED(result) && legacy != nullptr)
            {
                result = legacy->QueryInterface(GameInputApi::IID_IGameInput, reinterpret_cast<void **>(&nativeGameInput));
            }
            else if(SUCCEEDED(result))
            {
                result = E_FAIL;
            }
        }

        InterfaceReference<GameInputApi::IGameInput> gameInput(nativeGameInput);
        if(FAILED(result) || gameInput == nullptr)
        {
            return nativeStatus(
                static_cast<std::uint32_t>(FAILED(result) ? result : E_FAIL),
                GameWIP::Status::Types::NativeCodeDomain::HResult);
        }

        // Register controller-capable devices, then consume the initial notifications.
        constexpr std::uint32_t kInputKinds =
            static_cast<std::uint32_t>(GameInputApi::GameInputKindController) | static_cast<std::uint32_t>(GameInputApi::GameInputKindKeyboard) |
            static_cast<std::uint32_t>(GameInputApi::GameInputKindMouse) | static_cast<std::uint32_t>(GameInputApi::GameInputKindArcadeStick) |
            static_cast<std::uint32_t>(GameInputApi::GameInputKindFlightStick) | static_cast<std::uint32_t>(GameInputApi::GameInputKindGamepad) |
            static_cast<std::uint32_t>(GameInputApi::GameInputKindRacingWheel) | static_cast<std::uint32_t>(GameInputApi::GameInputKindUiNavigation);
        const GameInputApi::GameInputKind inputKinds = static_cast<GameInputApi::GameInputKind>(kInputKinds);
        result = gameInput->RegisterDeviceCallback(
            nullptr,
            inputKinds,
            GameInputApi::GameInputDeviceAnyStatus,
            GameInputApi::GameInputBlockingEnumeration,
            &attachment,
            gameInputDeviceCallback,
            &attachment.gameInputCallbackToken);
        if(FAILED(result))
        {
            return nativeStatus(
                static_cast<std::uint32_t>(result),
                GameWIP::Status::Types::NativeCodeDomain::HResult);
        }

        attachment.gameInputModule = moduleReference.release();
        attachment.gameInput = gameInput.release();
        return pollGameInput(attachment, state);
    }

    void shutdownGameInput(AttachmentState &attachment) noexcept
    {
        // Stop callbacks before releasing queued and registered device references.
        auto *gameInput = static_cast<GameInputApi::IGameInput *>(attachment.gameInput);
        if(gameInput != nullptr && attachment.gameInputCallbackToken != 0)
        {
            (void)gameInput->UnregisterCallback(attachment.gameInputCallbackToken);
            attachment.gameInputCallbackToken = 0;
        }

        // Drain callback notifications retained by the attachment.
        std::array<GameInputDeviceReference, AttachmentState::kGameInputEventCapacity> queuedDevices{};
        std::size_t queuedDeviceCount = 0;

        AcquireSRWLockExclusive(&attachment.gameInputEventLock);
        while(attachment.gameInputEventCount != 0)
        {
            queuedDevices[queuedDeviceCount++].reset(
                static_cast<GameInputApi::IGameInputDevice *>(attachment.gameInputEvents[attachment.gameInputEventHead].device));
            attachment.gameInputEvents[attachment.gameInputEventHead].device = nullptr;
            attachment.gameInputEventHead = (attachment.gameInputEventHead + 1) % attachment.gameInputEvents.size();
            --attachment.gameInputEventCount;
        }
        attachment.gameInputEventHead = 0;
        attachment.gameInputEventOverflow = false;
        ReleaseSRWLockExclusive(&attachment.gameInputEventLock);

        for(std::size_t index = 0; index < queuedDeviceCount; ++index)
        {
            queuedDevices[index].reset();
        }

        // Release native device references before the provider interface.
        for(DeviceBinding &binding : attachment.devices)
        {
            if((binding.role == DeviceRole::GameInputController || binding.role == DeviceRole::GameInputGamepad) && binding.nativeDevice != nullptr)
            {
                static_cast<GameInputApi::IGameInputDevice *>(binding.nativeDevice)->Release();
                binding.nativeDevice = nullptr;
            }
        }

        if(gameInput != nullptr)
        {
            gameInput->Release();
        }

        if(attachment.gameInputModule != nullptr)
        {
            FreeLibrary(attachment.gameInputModule);
        }
        attachment.gameInput = nullptr;
        attachment.gameInputModule = nullptr;
        attachment.gameInputCallbackToken = 0;
    }

    GameWIP::Status::Types::Status pollGameInput(AttachmentState &attachment, SystemState &state) noexcept
    {
        if(attachment.gameInput == nullptr || attachment.system == nullptr)
        {
            return status(GameWIP::Status::Types::Code::Unsupported);
        }

        try
        {
            // Move callback notifications to local storage before processing them.
            std::array<AttachmentState::GameInputDeviceEvent, AttachmentState::kGameInputEventCapacity> events{};
            std::array<GameInputDeviceReference, AttachmentState::kGameInputEventCapacity> eventDevices{};
            std::size_t eventCount = 0;
            bool overflow = false;

            // Snapshot notifications while holding the callback lock.
            AcquireSRWLockExclusive(&attachment.gameInputEventLock);
            eventCount = attachment.gameInputEventCount;
            overflow = attachment.gameInputEventOverflow;
            for(std::size_t index = 0; index < eventCount; ++index)
            {
                events[index] = attachment.gameInputEvents[attachment.gameInputEventHead];
                eventDevices[index].reset(static_cast<GameInputApi::IGameInputDevice *>(events[index].device));
                attachment.gameInputEvents[attachment.gameInputEventHead].device = nullptr;
                attachment.gameInputEventHead = (attachment.gameInputEventHead + 1) % attachment.gameInputEvents.size();
            }
            attachment.gameInputEventCount = 0;
            attachment.gameInputEventOverflow = false;
            ReleaseSRWLockExclusive(&attachment.gameInputEventLock);

            if(overflow)
            {
                ++state.health.totalDroppedEvents;
                state.pendingDiscontinuity = true;
            }

            // Apply arrivals and removals before polling current device state.
            for(std::size_t index = 0; index < eventCount; ++index)
            {
                GameInputApi::IGameInputDevice *device = eventDevices[index].get();
                if(device == nullptr)
                {
                    continue;
                }

                const bool connected = (events[index].currentStatus & static_cast<std::uint32_t>(GameInputApi::GameInputDeviceConnected)) != 0;
                const bool wasConnected = (events[index].previousStatus & static_cast<std::uint32_t>(GameInputApi::GameInputDeviceConnected)) != 0;
                if(connected)
                {
                    registerGameInputDevice(state, attachment, device);
                }
                else if(wasConnected)
                {
                    disconnectGameInputDevice(state, attachment, device);
                }
                eventDevices[index].reset();
            }

            // Read current controller snapshots and publish their available controls.
            auto *gameInput = static_cast<GameInputApi::IGameInput *>(attachment.gameInput);
            for(DeviceBinding &binding : attachment.devices)
            {
                if((binding.role != DeviceRole::GameInputController && binding.role != DeviceRole::GameInputGamepad) || !binding.connected ||
                   binding.nativeDevice == nullptr)
                {
                    continue;
                }

                auto *device = static_cast<GameInputApi::IGameInputDevice *>(binding.nativeDevice);
                if((device->GetDeviceStatus() & GameInputApi::GameInputDeviceConnected) == 0)
                {
                    invalidateDevice(state, attachment, binding, Types::EventCause::Disconnected);
                    continue;
                }

                const auto inputKinds = static_cast<GameInputApi::GameInputKind>(binding.nativeKind);
                GameInputApi::IGameInputReading *nativeReading = nullptr;
                const HRESULT result = gameInput->GetCurrentReading(inputKinds, device, &nativeReading);
                GameInputReadingReference reading(nativeReading);
                if(FAILED(result) || reading == nullptr)
                {
                    continue;
                }

                if(binding.role == DeviceRole::GameInputGamepad)
                {
                    publishGameInputGamepad(state, binding, *reading);
                }
                publishGameInputController(state, binding, *reading);
            }

            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            ++state.health.totalBackendFailures;
            state.pendingDiscontinuity = true;
            return status(GameWIP::Status::Types::Code::OutOfMemory);
        }
    }
} // namespace GameWIP::Input::Platform::Win32
