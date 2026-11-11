/// @file win32_attachment_state.h
/// @brief Private Win32 attachment state and cross-translation-unit helpers.
#pragma once

#include "input/internal/system_state.h"
#include "input/internal/value.h"
#include "input/standard.h"
#include "input/win32.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/platform/win32/dynamic_library.h"

#include <windows.h>
#include <windowsx.h>
#include <dbt.h>
#include <hidsdi.h>
#include <hidusage.h>
#include <imm.h>
#include <xinput.h>

#ifndef HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER
#define HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER 0x08
#endif

namespace GameWIP::Input::Platform::Win32
{
    struct Attachment::Impl
    {
        enum class ElementRole : std::uint8_t
        {
            Standard,
            MouseMovement,
            MouseWheel,
            PointerPosition,
            LeftStick,
            RightStick,
            Trigger
        };

        enum class DeviceRole : std::uint8_t
        {
            Keyboard,
            Mouse,
            Pointer,
            Gamepad,
            Hid
        };

        struct ElementBinding
        {
            Types::ElementId id{};
            ElementRole role = ElementRole::Standard;
            Types::StandardId standard{};
            Types::HidControlId hid{};
        };

        struct DeviceBinding
        {
            DeviceRole role = DeviceRole::Keyboard;
            std::uint32_t nativeSlot = 0;
            void *nativeDevice = nullptr;
            Types::DeviceId id{};
            bool connected = false;
            std::size_t sourceIndex = 0;
            std::vector<ElementBinding> elements{};

            struct HidElement
            {
                Types::ElementId id{};
                USAGE usagePage = 0;
                USAGE usage = 0;
                USHORT linkCollection = 0;
                UCHAR reportId = 0;
                std::uint32_t component = 0;
                bool button = false;
                bool absolute = true;
                bool signedValue = false;
                std::int32_t logicalMinimum = 0;
                std::int32_t logicalMaximum = 0;
                std::uint16_t bitSize = 0;
            };

            struct NormalizedElement
            {
                Types::ElementId id{};
                Types::ElementId sourceX{};
                Types::ElementId sourceY{};
                Types::StandardId standard{};
                float x = 0.0F;
                float y = 0.0F;
                std::uint8_t hatDirection = 0;
                bool isHat = false;
            };

            std::vector<HidElement> hidElements{};
            std::vector<NormalizedElement> normalizedElements{};
            std::vector<std::byte> hidPreparsedData{};
            std::vector<USAGE_AND_PAGE> hidUsageScratch{};
            std::uint32_t hidReportLength = 0;
            bool hidOpaque = false;
        };

        System *system = nullptr;
        void *windowHandle = nullptr;
        bool attached = false;
        std::vector<DeviceBinding> devices{};
        char16_t pendingHighSurrogate = 0;
        std::vector<std::byte> rawInputBuffer{};

        using XInputGetStateFunction = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
        HMODULE xinputModule = nullptr;
        XInputGetStateFunction xinputGetState = nullptr;
        bool xinputLoadAttempted = false;
        bool xinputFailureReported = false;
        std::array<DWORD, 4> xinputPackets{};
        std::array<bool, 4> xinputHasPackets{};
        HDEVNOTIFY hidDeviceNotification = nullptr;
        std::vector<std::pair<USHORT, USHORT>> rawInputCollections{};
    };

    namespace Detail
    {
        using IO::Types::ErrorCode;
        using DeviceBinding = Attachment::Impl::DeviceBinding;
        using DeviceRole = Attachment::Impl::DeviceRole;
        using ElementBinding = Attachment::Impl::ElementBinding;
        using ElementRole = Attachment::Impl::ElementRole;

        extern std::atomic<Attachment::Impl *> processRegistrationOwner;

        [[nodiscard]] IO::Types::Status status(ErrorCode code) noexcept;
        [[nodiscard]] IO::Types::Status nativeStatus(std::uint32_t code) noexcept;
        [[nodiscard]] Types::Timestamp now() noexcept;
        [[nodiscard]] std::uint64_t surfaceValue(const void *windowHandle) noexcept;

        [[nodiscard]] Types::ElementState *findState(System::Impl &state, Types::ElementId id) noexcept;
        [[nodiscard]] Types::ElementInfo *findElement(System::Impl &state, Types::ElementId id) noexcept;
        [[nodiscard]] DeviceBinding *findDevice(Attachment::Impl &attachment, DeviceRole role, std::uint32_t slot) noexcept;
        [[nodiscard]] DeviceBinding *findNativeDevice(Attachment::Impl &attachment, DeviceRole role, void *nativeDevice) noexcept;
        [[nodiscard]] DeviceBinding *findHidDevice(Attachment::Impl &attachment, void *nativeDevice) noexcept;
        [[nodiscard]] ElementBinding *findElement(DeviceBinding &device, Types::StandardId standard) noexcept;
        [[nodiscard]] ElementBinding *findHidElement(DeviceBinding &device, Types::HidControlId hid) noexcept;
        [[nodiscard]] ElementBinding *findRole(DeviceBinding &device, ElementRole role) noexcept;
        [[nodiscard]] ElementBinding *standardElement(DeviceBinding &device, Types::StandardId standard) noexcept;

        void invalidateDevice(System::Impl &state, Attachment::Impl &attachment, DeviceBinding &device, Types::EventCause cause) noexcept;
        void reconnectDevice(System::Impl &state, DeviceBinding &device) noexcept;
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
            std::optional<Types::HidControlId> hid = std::nullopt);
        void addDevice(
            System::Impl &state,
            Attachment::Impl &attachment,
            DeviceRole role,
            std::uint32_t slot,
            std::string name,
            std::string manufacturer,
            bool connected,
            Types::SourceInfo source = {});

        void appendElementEvent(
            System::Impl &state,
            Types::ElementId id,
            Types::Value value,
            Types::Value previous,
            bool previousKnown,
            Types::EventCause cause = Types::EventCause::Reported) noexcept;
        void publish(
            System::Impl &state,
            Types::ElementId id,
            const Types::Value &value,
            bool relative,
            Types::Timestamp timestamp = {},
            Types::EventCause cause = Types::EventCause::Reported) noexcept;
        void appendRawReport(
            System::Impl &state,
            const Types::Configuration &configuration,
            Types::DeviceId device,
            Types::BackendKind backend,
            std::span<const std::byte> bytes) noexcept;
        void invalidateFocus(System::Impl &state, Attachment::Impl &attachment) noexcept;
        void appendTextUtf8(
            System::Impl &state,
            Attachment::Impl &attachment,
            std::string_view utf8,
            Types::EventKind kind,
            std::size_t selectionStartBytes = 0,
            std::size_t selectionLengthBytes = 0,
            bool canceled = false) noexcept;
        void appendText(System::Impl &state, Attachment::Impl &attachment, char32_t scalar) noexcept;
        void handleUtf16(System::Impl &state, Attachment::Impl &attachment, char16_t codeUnit) noexcept;
        [[nodiscard]] IO::Types::Status checkAttached(const Attachment::Impl &attachment) noexcept;

        void appendImeText(
            System::Impl &state,
            Attachment::Impl &attachment,
            HIMC context,
            DWORD compositionFlag,
            Types::EventKind kind,
            LONG cursorPosition = 0) noexcept;
        [[nodiscard]] std::uint32_t rawKeyboardUsage(const RAWKEYBOARD &keyboard) noexcept;
        [[nodiscard]] float xInputTrigger(BYTE value) noexcept;
        [[nodiscard]] float xInputAxis(SHORT value) noexcept;
        void loadXInput(Attachment::Impl &attachment) noexcept;
        void releaseXInput(Attachment::Impl &attachment) noexcept;
        void publishNormalizedGamepad(
            System::Impl &state,
            DeviceBinding &device,
            const Attachment::Impl::DeviceBinding::HidElement &source,
            const Types::Value &value) noexcept;
        [[nodiscard]] IO::Types::Status registerRawInputCollection(Attachment::Impl &attachment, USHORT usagePage, USHORT usage, DWORD flags);
        [[nodiscard]] IO::Types::Status registerHidCollections(Attachment::Impl &attachment);
        [[nodiscard]] bool registerHidDeviceNotification(Attachment::Impl &attachment) noexcept;
        void unregisterHidDeviceNotification(Attachment::Impl &attachment) noexcept;
        void unregisterRawInput(Attachment::Impl &attachment) noexcept;
        [[nodiscard]] bool enumerateNativeDevices(System::Impl &state, Attachment::Impl &attachment);
        [[nodiscard]] bool addRawDevice(System::Impl &state, Attachment::Impl &attachment, DeviceRole role, HANDLE nativeDevice);
        [[nodiscard]] bool addHidDevice(System::Impl &state, Attachment::Impl &attachment, HANDLE nativeDevice);
        [[nodiscard]] bool addOpaqueHidDevice(System::Impl &state, Attachment::Impl &attachment, HANDLE nativeDevice);
        void publishHidReport(System::Impl &state, Attachment::Impl &attachment, DeviceBinding &device, std::span<const std::byte> report) noexcept;
    } // namespace Detail
} // namespace GameWIP::Input::Platform::Win32
