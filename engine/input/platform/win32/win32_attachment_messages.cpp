/// @file win32_attachment_messages.cpp
/// @brief Routes Win32 window messages to the Input attachment.

#include "input/platform/win32/internal/win32_attachment_state.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Input::Platform::Win32
{
    using namespace Detail;

    // ------------------------------------------------------------
    // Window message routing
    // ------------------------------------------------------------

    IO::Types::Status Attachment::handleMessage(std::uint32_t message, std::uint64_t wParam, std::int64_t lParam, bool &outHandled) noexcept
    {
        outHandled = false;

        // Validate the attachment before routing the message.
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status ownerStatus = checkAttached(*impl_);
        if(!ownerStatus.ok())
        {
            return ownerStatus;
        }

        try
        {
            System::Impl &state = *impl_->system->impl_;

            // Read and validate the native packet before dispatching by device type.
            if(message == WM_INPUT)
            {
                UINT byteCount = 0;
                HRAWINPUT rawHandle = reinterpret_cast<HRAWINPUT>(static_cast<std::uintptr_t>(lParam));

                if(GetRawInputData(rawHandle, RID_INPUT, nullptr, &byteCount, sizeof(RAWINPUTHEADER)) != 0 || byteCount == 0)
                {
                    ++state.health.totalInvalidReports;
                    state.pendingDiscontinuity = true;
                    return status(ErrorCode::NativeFailure);
                }
                impl_->rawInputBuffer.resize(byteCount);
                UINT received = byteCount;

                if(GetRawInputData(rawHandle, RID_INPUT, impl_->rawInputBuffer.data(), &received, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
                {
                    ++state.health.totalInvalidReports;
                    state.pendingDiscontinuity = true;
                    return nativeStatus(GetLastError());
                }

                if(received < sizeof(RAWINPUTHEADER) || received > byteCount)
                {
                    ++state.health.totalInvalidReports;
                    state.pendingDiscontinuity = true;
                    return status(ErrorCode::NativeFailure);
                }
                const std::span<const std::byte> rawBytes = std::span<const std::byte>(impl_->rawInputBuffer).first(received);
                std::array<RAWINPUT, 1> rawStorage{};
                std::span<std::byte> rawStorageBytes = std::as_writable_bytes(std::span(rawStorage));
                std::ranges::copy(rawBytes.first(std::min(rawBytes.size(), rawStorageBytes.size())), rawStorageBytes.begin());
                const RAWINPUT &raw = rawStorage.front();

                // Process the keyboard report.
                if(raw.header.dwType == RIM_TYPEKEYBOARD)
                {
                    // Validate the packet size.
                    if(received < offsetof(RAWINPUT, data.keyboard) + sizeof(RAWKEYBOARD))
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        return status(ErrorCode::NativeFailure);
                    }

                    // Resolve the keyboard device.
                    DeviceBinding *keyboard = findNativeDevice(*impl_, DeviceRole::Keyboard, raw.header.hDevice);
                    if(keyboard == nullptr && raw.header.hDevice != nullptr)
                    {
                        (void)addRawDevice(state, *impl_, DeviceRole::Keyboard, raw.header.hDevice);
                        keyboard = findNativeDevice(*impl_, DeviceRole::Keyboard, raw.header.hDevice);
                    }

                    if(keyboard == nullptr)
                    {
                        keyboard = findDevice(*impl_, DeviceRole::Keyboard, 0);
                    }

                    // Capture the raw report and decode the usage.
                    if(keyboard != nullptr)
                    {
                        appendRawReport(state, state.configuration, keyboard->id, Types::BackendKind::Win32RawInput, rawBytes);
                    }
                    const std::uint32_t usage = rawKeyboardUsage(raw.data.keyboard);
                    if(keyboard == nullptr || usage == 0)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        return status(ErrorCode::Success);
                    }

                    // Find the reported key control.
                    const Types::StandardId standard{Types::StandardDomain::Keyboard, usage};
                    ElementBinding *element = standardElement(*keyboard, standard);
                    if(element == nullptr)
                    {
                        // Try the matching HID usage when no standard control exists.
                        const Types::HidControlId hid{
                            .usagePage = 0x07,
                            .usage = usage,
                        };
                        element = findHidElement(*keyboard, hid);
                        if(element == nullptr)
                        {
                            // Add HID control metadata when the usage is new.
                            const std::size_t deviceIndex = state.findDeviceIndex(keyboard->id);
                            if(deviceIndex == state.devices.size())
                            {
                                ++state.health.totalInvalidReports;
                                state.pendingDiscontinuity = true;
                                return status(ErrorCode::Success);
                            }
                            Types::DeviceInfo &descriptor = state.devices[deviceIndex];

                            // Build a source-specific persistent key.
                            std::string persistentKey;
                            std::string_view sourcePersistentKey = descriptor.persistentKey;
                            if(keyboard->sourceIndex < descriptor.sources.size() && !descriptor.sources[keyboard->sourceIndex].persistentKey.empty())
                            {
                                sourcePersistentKey = descriptor.sources[keyboard->sourceIndex].persistentKey;
                            }

                            if(!sourcePersistentKey.empty())
                            {
                                persistentKey = std::string(sourcePersistentKey) + ":control:7:" + std::to_string(usage);
                            }

                            // Add the boolean element to the descriptor.
                            addElement(
                                state,
                                descriptor,
                                *keyboard,
                                "Keyboard HID Usage " + std::to_string(usage),
                                std::move(persistentKey),
                                std::nullopt,
                                Types::ValueKind::Boolean,
                                Types::ReportingMode::Absolute,
                                {},
                                ElementRole::Standard,
                                hid);

                            // Publish the updated descriptor.
                            descriptor.descriptorRevision = ++state.deviceRevision;
                            state.appendEvent({
                                .timestamp = now(),
                                .kind = Types::EventKind::DeviceChanged,
                                .cause = Types::EventCause::SourceChanged,
                                .data = Types::DeviceEvent{keyboard->id, descriptor.descriptorRevision},
                            });
                            element = findHidElement(*keyboard, hid);
                        }
                    }

                    if(element == nullptr)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        return status(ErrorCode::Success);
                    }

                    // Publish the key state and keyboard fallback transition.
                    const bool down = (raw.data.keyboard.Flags & RI_KEY_BREAK) == 0;
                    publish(state, element->id, down, false);
                    noteRawKeyboardTransition(
                        *impl_,
                        usage,
                        (raw.data.keyboard.Flags & RI_KEY_E0) != 0,
                        down,
                        static_cast<std::uint32_t>(GetMessageTime()));
                    outHandled = true;
                    return IO::successStatus();
                }

                // Publish mouse movement, wheel, and button changes.
                if(raw.header.dwType == RIM_TYPEMOUSE)
                {
                    if(received < offsetof(RAWINPUT, data.mouse) + sizeof(RAWMOUSE))
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        return status(ErrorCode::NativeFailure);
                    }

                    // Resolve the mouse device.
                    DeviceBinding *mouse = findNativeDevice(*impl_, DeviceRole::Mouse, raw.header.hDevice);
                    if(mouse == nullptr && raw.header.hDevice != nullptr)
                    {
                        (void)addRawDevice(state, *impl_, DeviceRole::Mouse, raw.header.hDevice);
                        mouse = findNativeDevice(*impl_, DeviceRole::Mouse, raw.header.hDevice);
                    }

                    if(mouse == nullptr)
                    {
                        mouse = findDevice(*impl_, DeviceRole::Mouse, 0);
                    }

                    if(mouse == nullptr)
                    {
                        return status(ErrorCode::Success);
                    }
                    const RAWMOUSE &data = raw.data.mouse;
                    const bool relative = (data.usFlags & MOUSE_MOVE_ABSOLUTE) == 0;

                    // Publish relative movement.
                    if(relative && (data.lLastX != 0 || data.lLastY != 0))
                    {
                        if(ElementBinding *movement = findRole(*mouse, ElementRole::MouseMovement); movement != nullptr)
                        {
                            publish(state, movement->id, Types::Vector2I64{data.lLastX, data.lLastY}, true);
                        }
                    }

                    // Publish wheel changes.
                    if((data.usButtonFlags & RI_MOUSE_WHEEL) != 0 || (data.usButtonFlags & RI_MOUSE_HWHEEL) != 0)
                    {
                        if(ElementBinding *wheel = findRole(*mouse, ElementRole::MouseWheel); wheel != nullptr)
                        {
                            publish(
                                state,
                                wheel->id,
                                Types::Vector2F32{
                                    (data.usButtonFlags & RI_MOUSE_WHEEL) != 0
                                        ? static_cast<float>(static_cast<SHORT>(data.usButtonData)) / WHEEL_DELTA
                                        : 0.0F,
                                    (data.usButtonFlags & RI_MOUSE_HWHEEL) != 0
                                        ? static_cast<float>(static_cast<SHORT>(data.usButtonData)) / WHEEL_DELTA
                                        : 0.0F},
                                true);
                        }
                    }

                    // Map button flags to standard controls.
                    const std::array<Types::StandardId, 5> buttons{
                        {Standard::Mouse::Left, Standard::Mouse::Right, Standard::Mouse::Middle, Standard::Mouse::X1, Standard::Mouse::X2}};
                    const std::array<std::pair<USHORT, USHORT>, 5> transitions{
                        {{RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP},
                         {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP},
                         {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP},
                         {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP},
                         {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP}}};

                    for(std::size_t index = 0; index < buttons.size(); ++index)
                    {
                        if((data.usButtonFlags & transitions[index].first) != 0 || (data.usButtonFlags & transitions[index].second) != 0)
                        {
                            if(ElementBinding *button = standardElement(*mouse, buttons[index]); button != nullptr)
                            {
                                publish(state, button->id, (data.usButtonFlags & transitions[index].first) != 0, false);
                            }
                        }
                    }

                    // Preserve the native report.
                    appendRawReport(state, state.configuration, mouse->id, Types::BackendKind::Win32RawInput, rawBytes);
                    outHandled = true;
                    return IO::successStatus();
                }

                // Route the HID report.
                if(raw.header.dwType == RIM_TYPEHID)
                {
                    const std::size_t reportDataOffset = offsetof(RAWINPUT, data.hid.bRawData);
                    if(received < reportDataOffset)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return status(ErrorCode::NativeFailure);
                    }

                    // Resolve or register the HID device.
                    DeviceBinding *hid = findHidDevice(*impl_, raw.header.hDevice);
                    if(hid == nullptr && !addHidDevice(state, *impl_, raw.header.hDevice))
                    {
                        (void)addOpaqueHidDevice(state, *impl_, raw.header.hDevice);
                        hid = findHidDevice(*impl_, raw.header.hDevice);
                    }

                    if(hid == nullptr)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return IO::successStatus();
                    }

                    // Validate each report's bounds and descriptor size.
                    if(raw.data.hid.dwSizeHid == 0)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return IO::successStatus();
                    }
                    const std::size_t reportSize = raw.data.hid.dwSizeHid;
                    const std::size_t reportCount = raw.data.hid.dwCount;
                    if(reportSize == 0 || reportCount > (static_cast<std::size_t>(received) - reportDataOffset) / reportSize ||
                       (hid->hidReportLength != 0 && reportSize != hid->hidReportLength))
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return status(ErrorCode::NativeFailure);
                    }

                    // Decode every report in the packet.
                    for(std::size_t index = 0; index < reportCount; ++index)
                    {
                        publishHidReport(state, *impl_, *hid, rawBytes.subspan(reportDataOffset + index * reportSize, reportSize));
                    }

                    outHandled = true;
                    return IO::successStatus();
                }
                return IO::successStatus();
            }

            // Refresh device discovery after system-level changes.
            if(message == WM_DEVICECHANGE)
            {
                if(wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVNODES_CHANGED)
                {
                    if(impl_->hidEnabled)
                    {
                        const IO::Types::Status registrationStatus = registerHidCollections(*impl_);
                        if(!registrationStatus.ok())
                        {
                            ++state.health.totalBackendFailures;
                            state.pendingDiscontinuity = true;
                            outHandled = true;
                            return registrationStatus;
                        }
                    }

                    if((impl_->rawInputEnabled || impl_->hidEnabled) && !enumerateNativeDevices(state, *impl_))
                    {
                        ++state.health.totalBackendFailures;
                        state.pendingDiscontinuity = true;
                    }

                    if(impl_->directInputEnabled)
                    {
                        const IO::Types::Status enumerationStatus = enumerateDirectInputDevices(*impl_, state);
                        if(!enumerationStatus.ok())
                        {
                            ++state.health.totalBackendFailures;
                            state.pendingDiscontinuity = true;
                        }
                    }
                }
                outHandled = true;
                return IO::successStatus();
            }

            // Handle Raw Input arrival and removal notifications.
            if(message == WM_INPUT_DEVICE_CHANGE)
            {
                if(wParam == GIDC_ARRIVAL)
                {
                    if((impl_->rawInputEnabled || impl_->hidEnabled) && !enumerateNativeDevices(state, *impl_))
                    {
                        ++state.health.totalBackendFailures;
                        state.pendingDiscontinuity = true;
                    }

                    if(impl_->directInputEnabled)
                    {
                        const IO::Types::Status enumerationStatus = enumerateDirectInputDevices(*impl_, state);
                        if(!enumerationStatus.ok())
                        {
                            ++state.health.totalBackendFailures;
                            state.pendingDiscontinuity = true;
                        }
                    }
                }
                else if(wParam == GIDC_REMOVAL)
                {
                    void *nativeDevice = reinterpret_cast<void *>(static_cast<std::uintptr_t>(lParam));
                    for(const DeviceRole role : {DeviceRole::Keyboard, DeviceRole::Mouse, DeviceRole::Hid})
                    {
                        DeviceBinding *device = findNativeDevice(*impl_, role, nativeDevice);
                        if(device != nullptr)
                        {
                            invalidateDevice(state, *impl_, *device, Types::EventCause::Disconnected);
                        }
                    }
                }
                outHandled = true;
                return IO::successStatus();
            }

            // Release held controls after focus loss.
            if(message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && wParam == 0))
            {
                invalidateFocus(state, *impl_);
                outHandled = true;
                return IO::successStatus();
            }

            // Publish the Window pointer position.
            if(message == WM_MOUSEMOVE)
            {
                if(DeviceBinding *pointer = findDevice(*impl_, DeviceRole::Pointer, 0); pointer != nullptr)
                {
                    if(ElementBinding *position = findRole(*pointer, ElementRole::PointerPosition); position != nullptr)
                    {
                        publish(
                            state,
                            position->id,
                            Types::Vector2I64{static_cast<std::int64_t>(GET_X_LPARAM(lParam)), static_cast<std::int64_t>(GET_Y_LPARAM(lParam))},
                            false);
                    }
                }
                outHandled = true;
                return IO::successStatus();
            }

            // Queue Window keyboard transitions as a fallback source.
            if(impl_->windowMessagesEnabled && (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP))
            {
                const auto scanCode = static_cast<unsigned int>((static_cast<std::uintptr_t>(lParam) >> 16U) & 0xFFU);
                const bool extended = (static_cast<std::uintptr_t>(lParam) & (std::uintptr_t{1} << 24U)) != 0;
                RAWKEYBOARD keyboard{};
                keyboard.MakeCode = static_cast<USHORT>(scanCode);
                keyboard.Flags = extended ? RI_KEY_E0 : 0;
                keyboard.VKey = static_cast<USHORT>(wParam);
                if(keyboard.VKey == VK_PAUSE)
                {
                    keyboard.Flags |= RI_KEY_E1;
                }

                const std::uint32_t usage = rawKeyboardUsage(keyboard);
                queueWindowKeyboardTransition(
                    state,
                    *impl_,
                    usage,
                    extended,
                    message == WM_KEYDOWN || message == WM_SYSKEYDOWN,
                    static_cast<std::uint32_t>(GetMessageTime()));
                return IO::successStatus();
            }

            // Forward text and IME composition.
            if(message == WM_IME_STARTCOMPOSITION)
            {
                appendTextUtf8(state, *impl_, {}, Types::EventKind::CompositionStarted);
                outHandled = true;
                return IO::successStatus();
            }

            if(message == WM_IME_COMPOSITION)
            {
                const HWND window = reinterpret_cast<HWND>(impl_->windowHandle);
                HIMC context = ImmGetContext(window);
                if(context != nullptr)
                {
                    const auto composition = static_cast<ULONG_PTR>(lParam);
                    const LONG cursorPosition =
                        (composition & static_cast<ULONG_PTR>(GCS_CURSORPOS)) != 0 ? ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0) : 0;
                    if((composition & static_cast<ULONG_PTR>(GCS_RESULTSTR)) != 0)
                    {
                        appendImeText(state, *impl_, context, GCS_RESULTSTR, Types::EventKind::TextCommitted);
                    }

                    if((composition & static_cast<ULONG_PTR>(GCS_COMPSTR)) != 0)
                    {
                        appendImeText(state, *impl_, context, GCS_COMPSTR, Types::EventKind::CompositionUpdated, cursorPosition);
                    }
                    ImmReleaseContext(window, context);
                }
                outHandled = true;
                return IO::successStatus();
            }

            if(message == WM_IME_ENDCOMPOSITION)
            {
                appendTextUtf8(state, *impl_, {}, Types::EventKind::CompositionFinished);
                outHandled = true;
                return IO::successStatus();
            }

            if(message == WM_CHAR)
            {
                handleUtf16(state, *impl_, static_cast<char16_t>(wParam));
                outHandled = true;
                return IO::successStatus();
            }

            if(message == WM_UNICHAR)
            {
                if(wParam != UNICODE_NOCHAR)
                {
                    if(impl_->pendingHighSurrogate != 0)
                    {
                        impl_->pendingHighSurrogate = 0;
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                    }
                    appendText(state, *impl_, static_cast<char32_t>(wParam));
                }
                outHandled = true;
                return IO::successStatus();
            }
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return status(ErrorCode::OutOfMemory);
        }
    }
} // namespace GameWIP::Input::Platform::Win32
