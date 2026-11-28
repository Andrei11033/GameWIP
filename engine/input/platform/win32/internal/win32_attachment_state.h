/// @file win32_attachment_state.h
/// @brief Private Win32 attachment state and cross-translation-unit helpers.
#pragma once

#include "input/internal/system_state.h"
#include "input/internal/value.h"
#include "input/standard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/platform/win32/dynamic_library.h"

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <windows.h>
#include <windowsx.h>
#include <dbt.h>
#include <hidsdi.h>
#include <hidusage.h>
#include <imm.h>
#include <xinput.h>
#include <dinput.h>

#ifndef HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER
#define HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER 0x08
#endif

namespace GameWIP::Input::Internal::Win32
{
    struct AttachmentState
    {
        // Element and device roles
        enum class ElementRole : std::uint8_t
        {
            Standard,
            MouseMovement,
            MouseWheel,
            PointerPosition,
            LeftStick,
            RightStick,
            Trigger,
            NativeAxis,
            NativeButton,
            NativeSwitch
        };

        enum class DeviceRole : std::uint8_t
        {
            Keyboard,
            Mouse,
            Pointer,
            Gamepad,
            Hid,
            MessageKeyboard,
            GameInputController,
            GameInputGamepad,
            DirectInputKeyboard,
            DirectInputMouse,
            DirectInputController
        };

        // Backend-to-Input bindings
        struct ElementBinding
        {
            Types::ElementId id{};
            ElementRole role = ElementRole::Standard;
            Types::StandardId standard{};
            Types::HidControlId hid{};
            std::uint32_t nativeIndex = 0;
        };

        struct DeviceBinding
        {
            // Native identity
            DeviceRole role = DeviceRole::Keyboard;
            std::uint32_t nativeSlot = 0;
            void *nativeDevice = nullptr;
            GUID directInputInstance{};
            std::uint32_t nativeKind = 0;

            // DirectInput polling state
            std::array<std::uint8_t, 256> directInputKeyboardState{};

            // Published Input binding
            Types::DeviceId id{};
            bool connected = false;
            std::size_t sourceIndex = 0;
            std::vector<ElementBinding> elements{};

            // HID descriptor elements
            struct HidElement
            {
                // HID identity
                Types::ElementId id{};
                USAGE usagePage = 0;
                USAGE usage = 0;
                USHORT linkCollection = 0;
                UCHAR reportId = 0;
                std::uint32_t component = 0;

                // Report value format
                bool button = false;
                bool absolute = true;
                bool signedValue = false;
                std::int32_t logicalMinimum = 0;
                std::int32_t logicalMaximum = 0;
                std::uint16_t bitSize = 0;
            };

            struct NormalizedElement
            {
                // Source and standard identities
                Types::ElementId id{};
                Types::ElementId sourceX{};
                Types::ElementId sourceY{};
                Types::StandardId standard{};

                // Derived state
                float x = 0.0F;
                float y = 0.0F;
                std::uint8_t hatDirection = 0;
                bool isHat = false;
            };

            // HID parser storage
            std::vector<HidElement> hidElements{};
            std::vector<NormalizedElement> normalizedElements{};
            std::vector<std::byte> hidPreparsedData{};
            std::vector<USAGE_AND_PAGE> hidUsageScratch{};
            std::uint32_t hidReportLength = 0;
            bool hidOpaque = false;
        };

        // Attached Input session
        System *system = nullptr;
        void *windowHandle = nullptr;
        bool attached = false;
        std::vector<DeviceBinding> devices{};

        // Enabled backends and registration state
        std::array<Types::BackendInfo, 6> backendStatuses{{
            {Types::BackendKind::Win32RawInput, Types::BackendStatus::Disabled, {}},
            {Types::BackendKind::Win32Hid, Types::BackendStatus::Disabled, {}},
            {Types::BackendKind::XInput, Types::BackendStatus::Disabled, {}},
            {Types::BackendKind::GameInput, Types::BackendStatus::Disabled, {}},
            {Types::BackendKind::DirectInput, Types::BackendStatus::Disabled, {}},
            {Types::BackendKind::Win32WindowMessage, Types::BackendStatus::Disabled, {}},
        }};
        bool rawInputEnabled = false;
        bool hidEnabled = false;
        bool xinputEnabled = false;
        bool gameInputEnabled = false;
        bool directInputEnabled = false;
        bool windowMessagesEnabled = false;

        // Window text and raw input buffers
        char16_t pendingHighSurrogate = 0;
        std::vector<std::byte> rawInputBuffer{};

        // XInput and HID registration
        using XInputGetStateFunction = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
        HMODULE xinputModule = nullptr;
        XInputGetStateFunction xinputGetState = nullptr;
        bool xinputLoadAttempted = false;
        bool xinputFailureReported = false;
        std::array<DWORD, 4> xinputPackets{};
        std::array<bool, 4> xinputHasPackets{};
        HDEVNOTIFY hidDeviceNotification = nullptr;
        std::vector<std::pair<USHORT, USHORT>> rawInputCollections{};

        // GameInput device callback queue
        struct GameInputDeviceEvent
        {
            void *device = nullptr;
            std::uint32_t currentStatus = 0;
            std::uint32_t previousStatus = 0;
        };

        static constexpr std::size_t kGameInputEventCapacity = 64;
        std::array<GameInputDeviceEvent, kGameInputEventCapacity> gameInputEvents{};
        SRWLOCK gameInputEventLock{};
        std::size_t gameInputEventHead = 0;
        std::size_t gameInputEventCount = 0;
        bool gameInputEventOverflow = false;

        // Keyboard transition correlation
        struct KeyboardTransition
        {
            std::uint32_t usage = 0;
            std::uint32_t messageTime = 0;
            std::uint8_t captureDelay = 0;
            bool extended = false;
            bool pressed = false;
            bool valid = false;
            bool matched = false;
        };

        static constexpr std::size_t kKeyboardTransitionCapacity = 64;
        std::array<KeyboardTransition, kKeyboardTransitionCapacity> rawKeyboardTransitions{};
        std::size_t rawKeyboardTransitionNext = 0;
        std::array<KeyboardTransition, kKeyboardTransitionCapacity> windowKeyboardTransitions{};
        std::size_t windowKeyboardTransitionHead = 0;
        std::size_t windowKeyboardTransitionCount = 0;
        bool windowKeyboardTransitionOverflow = false;

        // Dynamically loaded providers
        HMODULE gameInputModule = nullptr;
        void *gameInput = nullptr;
        std::uint64_t gameInputCallbackToken = 0;
        HMODULE directInputModule = nullptr;
        void *directInput = nullptr;
        bool directInputOwnsCom = false;
    };
} // namespace GameWIP::Input::Internal::Win32

namespace GameWIP::Input::Platform::Win32
{
    using ::GameWIP::Input::Internal::SystemState;
    using ::GameWIP::Input::Internal::Win32::AttachmentState;
    using GameWIP::Status::Types::Code;
    using DeviceBinding = AttachmentState::DeviceBinding;
    using DeviceRole = AttachmentState::DeviceRole;
    using ElementBinding = AttachmentState::ElementBinding;
    using ElementRole = AttachmentState::ElementRole;

    /// ------------------------------------------------------------
    /// Published values and raw reports
    /// ------------------------------------------------------------

    void publish(
        SystemState &state,
        Types::ElementId id,
        const Types::Value &value,
        bool relative,
        Types::Timestamp timestamp = {},
        Types::EventCause cause = Types::EventCause::Reported) noexcept;
    void appendRawReport(
        SystemState &state,
        const Types::Configuration &configuration,
        Types::DeviceId device,
        Types::BackendKind backend,
        std::span<const std::byte> bytes) noexcept;

    /// ------------------------------------------------------------
    /// Text input
    /// ------------------------------------------------------------

    void invalidateFocus(SystemState &state, AttachmentState &attachment) noexcept;
    void appendTextUtf8(
        SystemState &state,
        AttachmentState &attachment,
        std::string_view utf8,
        Types::EventKind kind,
        std::size_t selectionStartBytes = 0,
        std::size_t selectionLengthBytes = 0,
        bool canceled = false) noexcept;
    void appendText(SystemState &state, AttachmentState &attachment, char32_t scalar) noexcept;
    void handleUtf16(SystemState &state, AttachmentState &attachment, char16_t codeUnit) noexcept;

    /// ------------------------------------------------------------
    /// Backend selection and status
    /// ------------------------------------------------------------

    [[nodiscard]] bool backendSelected(const Types::Configuration &configuration, Types::BackendKind backend) noexcept;
    void setBackendStatus(AttachmentState &attachment, Types::BackendKind backend, Types::BackendStatus status, std::string_view detail = {});

    /// ------------------------------------------------------------
    /// GameInput provider
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status initializeGameInput(AttachmentState &attachment, SystemState &state) noexcept;
    void shutdownGameInput(AttachmentState &attachment) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status pollGameInput(AttachmentState &attachment, SystemState &state) noexcept;

    /// ------------------------------------------------------------
    /// DirectInput provider
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status initializeDirectInput(AttachmentState &attachment) noexcept;
    void shutdownDirectInput(AttachmentState &attachment) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status enumerateDirectInputDevices(AttachmentState &attachment, SystemState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status pollDirectInput(AttachmentState &attachment, SystemState &state) noexcept;

    /// ------------------------------------------------------------
    /// Keyboard mapping and Window-message fallback
    /// ------------------------------------------------------------

    void appendImeText(
        SystemState &state,
        AttachmentState &attachment,
        HIMC context,
        DWORD compositionFlag,
        Types::EventKind kind,
        LONG cursorPosition = 0) noexcept;
    [[nodiscard]] std::uint32_t rawKeyboardUsage(const RAWKEYBOARD &keyboard) noexcept;
    void noteRawKeyboardTransition(AttachmentState &attachment, std::uint32_t usage, bool extended, bool pressed, std::uint32_t messageTime) noexcept;
    void queueWindowKeyboardTransition(
        SystemState &state,
        AttachmentState &attachment,
        std::uint32_t usage,
        bool extended,
        bool pressed,
        std::uint32_t messageTime) noexcept;
    void processWindowKeyboardTransitions(SystemState &state, AttachmentState &attachment);

    /// ------------------------------------------------------------
    /// XInput
    /// ------------------------------------------------------------

    [[nodiscard]] float xInputTrigger(BYTE value) noexcept;
    [[nodiscard]] float xInputAxis(SHORT value) noexcept;
    void loadXInput(AttachmentState &attachment) noexcept;
    void releaseXInput(AttachmentState &attachment) noexcept;

    /// ------------------------------------------------------------
    /// Raw Input and HID discovery
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status registerRawInputCollection(AttachmentState &attachment, USHORT usagePage, USHORT usage, DWORD flags);
    [[nodiscard]] GameWIP::Status::Types::Status registerHidCollections(AttachmentState &attachment);
    [[nodiscard]] bool registerHidDeviceNotification(AttachmentState &attachment) noexcept;
    void unregisterHidDeviceNotification(AttachmentState &attachment) noexcept;
    void unregisterRawInput(AttachmentState &attachment) noexcept;
    [[nodiscard]] bool enumerateNativeDevices(SystemState &state, AttachmentState &attachment);
    [[nodiscard]] bool addRawDevice(SystemState &state, AttachmentState &attachment, DeviceRole role, HANDLE nativeDevice);
    [[nodiscard]] bool addHidDevice(SystemState &state, AttachmentState &attachment, HANDLE nativeDevice);
    [[nodiscard]] bool addOpaqueHidDevice(SystemState &state, AttachmentState &attachment, HANDLE nativeDevice);
    void publishHidReport(SystemState &state, DeviceBinding &device, std::span<const std::byte> report) noexcept;
} // namespace GameWIP::Input::Platform::Win32
