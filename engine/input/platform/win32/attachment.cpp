/// @file attachment.cpp
/// @brief Implements the public Win32 Input attachment lifecycle and message routing.

#include "input/platform/win32/internal/win32_attachment_state.h"

#include <algorithm>
#include <array>
#include <new>
#include <string>
#include <utility>

namespace GameWIP::Input::Platform::Win32
{
    using namespace Detail;

    // ------------------------------------------------------------
    // Attachment lifecycle and public message routing
    // ------------------------------------------------------------
    Attachment::Attachment() noexcept
        : impl_(std::unique_ptr<Impl>(new (std::nothrow) Impl{}))
    {
    }

    Attachment::~Attachment() noexcept
    {
        if(impl_ != nullptr)
        {
            (void)detach();
        }
    }

    IO::Types::Status Attachment::attach(System &system, void *windowHandle) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }
        if(impl_->attached)
        {
            return impl_->system == &system ? status(ErrorCode::AlreadyOpen) : status(ErrorCode::ResourceBusy);
        }
        if(windowHandle == nullptr)
        {
            return status(ErrorCode::InvalidArgument);
        }
        if(!system.isOpen())
        {
            return status(ErrorCode::NotOpen);
        }
        if(!system.ownedByCurrentThread())
        {
            return status(ErrorCode::ResourceBusy);
        }
        if(system.impl_->nativeBeforeCapture != nullptr)
        {
            return status(ErrorCode::AlreadyOpen);
        }
        Attachment::Impl *expected = nullptr;
        if(!processRegistrationOwner.compare_exchange_strong(expected, impl_.get(), std::memory_order_acq_rel))
        {
            return status(ErrorCode::ResourceBusy);
        }

        try
        {
            impl_->system = &system;
            impl_->windowHandle = windowHandle;
            impl_->attached = true;
            const IO::Types::Status rawInputStatus = registerRawInputCollection(*impl_, HID_USAGE_PAGE_GENERIC, 0, RIDEV_PAGEONLY | RIDEV_DEVNOTIFY);
            if(!rawInputStatus.ok())
            {
                unregisterRawInput(*impl_);
                impl_->system = nullptr;
                impl_->windowHandle = nullptr;
                impl_->attached = false;
                processRegistrationOwner.store(nullptr, std::memory_order_release);
                return rawInputStatus;
            }
            if(!registerHidDeviceNotification(*impl_))
            {
                const DWORD error = GetLastError();
                unregisterRawInput(*impl_);
                impl_->system = nullptr;
                impl_->windowHandle = nullptr;
                impl_->attached = false;
                processRegistrationOwner.store(nullptr, std::memory_order_release);
                return nativeStatus(error);
            }
            const IO::Types::Status hidCollectionsStatus = registerHidCollections(*impl_);
            if(!hidCollectionsStatus.ok())
            {
                unregisterHidDeviceNotification(*impl_);
                unregisterRawInput(*impl_);
                impl_->system = nullptr;
                impl_->windowHandle = nullptr;
                impl_->attached = false;
                processRegistrationOwner.store(nullptr, std::memory_order_release);
                return hidCollectionsStatus;
            }
            system.impl_->nativeBeforeCapture = &Attachment::beforeCapture;
            system.impl_->nativeAfterCapture = &Attachment::afterCapture;
            system.impl_->nativeClose = &Attachment::closeSystem;
            system.impl_->nativeContext = impl_.get();
            const bool enumerated = enumerateNativeDevices(*system.impl_, *impl_);
            const auto hasRole = [this](DeviceRole role)
            {
                return std::any_of(
                    impl_->devices.begin(),
                    impl_->devices.end(),
                    [role](const DeviceBinding &device)
                    {
                        return device.role == role;
                    });
            };
            if(!enumerated || !hasRole(DeviceRole::Keyboard))
            {
                addDevice(*system.impl_, *impl_, DeviceRole::Keyboard, 0, "Keyboard", {}, true);
            }
            if(!enumerated || !hasRole(DeviceRole::Mouse))
            {
                addDevice(*system.impl_, *impl_, DeviceRole::Mouse, 0, "Mouse", {}, true);
            }
            addDevice(
                *system.impl_,
                *impl_,
                DeviceRole::Pointer,
                0,
                "Window Pointer",
                {},
                true,
                Types::SourceInfo{
                    .backend = Types::BackendKind::Win32WindowMessage,
                    .backendName = "Win32 Window Messages",
                    .active = true,
                    .persistentKey = "win32:window:pointer:" + std::to_string(surfaceValue(impl_->windowHandle)),
                    .identityConfidence = Types::IdentityConfidence::ConnectionPath,
                });
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            unregisterHidDeviceNotification(*impl_);
            unregisterRawInput(*impl_);
            processRegistrationOwner.store(nullptr, std::memory_order_release);
            if(system.impl_->nativeContext == impl_.get())
            {
                system.impl_->nativeBeforeCapture = nullptr;
                system.impl_->nativeAfterCapture = nullptr;
                system.impl_->nativeClose = nullptr;
                system.impl_->nativeContext = nullptr;
            }
            impl_->system = nullptr;
            impl_->windowHandle = nullptr;
            impl_->attached = false;
            impl_->devices.clear();
            impl_->pendingHighSurrogate = 0;
            impl_->rawInputBuffer.clear();
            return status(ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status Attachment::detach() noexcept
    {
        if(impl_ == nullptr || !impl_->attached)
        {
            return IO::successStatus();
        }
        if(impl_->system != nullptr && impl_->system->isOpen())
        {
            const IO::Types::Status ownerStatus = checkAttached(*impl_);
            if(!ownerStatus.ok())
            {
                return ownerStatus;
            }
            for(DeviceBinding &device : impl_->devices)
            {
                invalidateDevice(*impl_->system->impl_, *impl_, device, Types::EventCause::Disconnected);
            }
            if(impl_->system->impl_->nativeContext == impl_.get())
            {
                impl_->system->impl_->nativeBeforeCapture = nullptr;
                impl_->system->impl_->nativeAfterCapture = nullptr;
                impl_->system->impl_->nativeClose = nullptr;
                impl_->system->impl_->nativeContext = nullptr;
            }
        }
        unregisterHidDeviceNotification(*impl_);
        unregisterRawInput(*impl_);
        releaseXInput(*impl_);
        Attachment::Impl *owner = impl_.get();
        processRegistrationOwner.compare_exchange_strong(owner, nullptr, std::memory_order_acq_rel);
        impl_->system = nullptr;
        impl_->windowHandle = nullptr;
        impl_->attached = false;
        impl_->devices.clear();
        impl_->pendingHighSurrogate = 0;
        impl_->rawInputBuffer.clear();
        return IO::successStatus();
    }

    bool Attachment::isAttached() const noexcept
    {
        return impl_ != nullptr && impl_->attached && impl_->system != nullptr && impl_->system->isOpen();
    }

    Types::SurfaceId Attachment::surfaceId() const noexcept
    {
        return isAttached() ? Types::SurfaceId{surfaceValue(impl_->windowHandle)} : Types::SurfaceId{};
    }

    IO::Types::Status Attachment::handleMessage(std::uint32_t message, std::uint64_t wParam, std::int64_t lParam, bool &outHandled) noexcept
    {
        outHandled = false;
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
                const RAWINPUT &raw = *reinterpret_cast<const RAWINPUT *>(impl_->rawInputBuffer.data());
                if(raw.header.dwType == RIM_TYPEKEYBOARD)
                {
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
                    const std::uint32_t usage = rawKeyboardUsage(raw.data.keyboard);
                    if(keyboard == nullptr || usage == 0)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        return status(ErrorCode::Success);
                    }
                    const Types::StandardId standard{Types::StandardDomain::Keyboard, usage};
                    ElementBinding *element = standardElement(*keyboard, standard);
                    if(element == nullptr)
                    {
                        const Types::HidControlId hid{
                            .usagePage = 0x07,
                            .usage = usage,
                        };
                        element = findHidElement(*keyboard, hid);
                        if(element == nullptr)
                        {
                            const std::size_t deviceIndex = state.findDeviceIndex(keyboard->id);
                            if(deviceIndex == state.devices.size())
                            {
                                ++state.health.totalInvalidReports;
                                state.pendingDiscontinuity = true;
                                return status(ErrorCode::Success);
                            }

                            Types::DeviceInfo &descriptor = state.devices[deviceIndex];
                            std::string persistentKey;
                            if(!descriptor.persistentKey.empty())
                            {
                                persistentKey = descriptor.persistentKey + ":control:7:" + std::to_string(usage);
                            }
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
                    const bool down = (raw.data.keyboard.Flags & RI_KEY_BREAK) == 0;
                    publish(state, element->id, down, false);
                    appendRawReport(
                        state,
                        state.configuration,
                        keyboard->id,
                        Types::BackendKind::Win32RawInput,
                        std::span<const std::byte>(impl_->rawInputBuffer.data(), static_cast<std::size_t>(received)));
                    outHandled = true;
                    return IO::successStatus();
                }
                if(raw.header.dwType == RIM_TYPEMOUSE)
                {
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
                    if(relative && (data.lLastX != 0 || data.lLastY != 0))
                    {
                        if(ElementBinding *movement = findRole(*mouse, ElementRole::MouseMovement); movement != nullptr)
                        {
                            publish(state, movement->id, Types::Vector2I64{data.lLastX, data.lLastY}, true);
                        }
                    }
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
                    appendRawReport(
                        state,
                        state.configuration,
                        mouse->id,
                        Types::BackendKind::Win32RawInput,
                        std::span<const std::byte>(impl_->rawInputBuffer.data(), static_cast<std::size_t>(received)));
                    outHandled = true;
                    return IO::successStatus();
                }
                if(raw.header.dwType == RIM_TYPEHID)
                {
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
                    if(raw.data.hid.dwSizeHid == 0)
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return IO::successStatus();
                    }
                    const std::size_t reportSize = raw.data.hid.dwSizeHid;
                    const std::size_t reportCount = raw.data.hid.dwCount;
                    const std::size_t reportDataOffset = offsetof(RAWINPUT, data.hid.bRawData);
                    if(reportSize == 0 || received < reportDataOffset ||
                       reportCount > (static_cast<std::size_t>(received) - reportDataOffset) / reportSize ||
                       (hid->hidReportLength != 0 && reportSize != hid->hidReportLength))
                    {
                        ++state.health.totalInvalidReports;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return status(ErrorCode::NativeFailure);
                    }
                    for(std::size_t index = 0; index < reportCount; ++index)
                    {
                        publishHidReport(state, *impl_, *hid, raw.data.hid.bRawData + index * reportSize, reportSize);
                    }
                    outHandled = true;
                    return IO::successStatus();
                }
                return IO::successStatus();
            }
            if(message == WM_DEVICECHANGE)
            {
                if(wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVNODES_CHANGED)
                {
                    const IO::Types::Status registrationStatus = registerHidCollections(*impl_);
                    if(!registrationStatus.ok())
                    {
                        ++state.health.totalBackendFailures;
                        state.pendingDiscontinuity = true;
                        outHandled = true;
                        return registrationStatus;
                    }
                    if(!enumerateNativeDevices(state, *impl_))
                    {
                        ++state.health.totalBackendFailures;
                        state.pendingDiscontinuity = true;
                    }
                }
                outHandled = true;
                return IO::successStatus();
            }
            if(message == WM_INPUT_DEVICE_CHANGE)
            {
                if(wParam == GIDC_ARRIVAL)
                {
                    if(!enumerateNativeDevices(state, *impl_))
                    {
                        ++state.health.totalBackendFailures;
                        state.pendingDiscontinuity = true;
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
            if(message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && wParam == 0))
            {
                invalidateFocus(state, *impl_);
                outHandled = true;
                return IO::successStatus();
            }
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

    IO::Types::Status Attachment::poll() noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = checkAttached(*impl_);
        return ownerStatus.ok() ? pollNative(*impl_) : ownerStatus;
    }

    IO::Types::Status Attachment::pollNative(Impl &implementation) noexcept
    {
        try
        {
            loadXInput(implementation);
            if(implementation.xinputGetState == nullptr)
            {
                if(!implementation.xinputFailureReported)
                {
                    ++implementation.system->impl_->health.totalBackendFailures;
                    implementation.xinputFailureReported = true;
                }
                return status(ErrorCode::Unsupported);
            }

            System::Impl &state = *implementation.system->impl_;
            for(DWORD slot = 0; slot < 4; ++slot)
            {
                XINPUT_STATE packet{};
                const DWORD result = implementation.xinputGetState(slot, &packet);
                DeviceBinding *gamepad = findDevice(implementation, DeviceRole::Gamepad, slot);
                if(result != ERROR_SUCCESS)
                {
                    if(gamepad != nullptr)
                    {
                        invalidateDevice(state, implementation, *gamepad, Types::EventCause::Disconnected);
                    }
                    implementation.xinputHasPackets[slot] = false;
                    continue;
                }
                if(gamepad == nullptr)
                {
                    addDevice(state, implementation, DeviceRole::Gamepad, slot, "XInput Controller " + std::to_string(slot + 1), {}, true);
                    gamepad = findDevice(implementation, DeviceRole::Gamepad, slot);
                }
                else if(!gamepad->connected)
                {
                    reconnectDevice(state, *gamepad);
                }
                if(gamepad == nullptr)
                {
                    continue;
                }
                if(implementation.xinputHasPackets[slot] && implementation.xinputPackets[slot] == packet.dwPacketNumber)
                {
                    continue;
                }
                implementation.xinputPackets[slot] = packet.dwPacketNumber;
                implementation.xinputHasPackets[slot] = true;
                const WORD buttons = packet.Gamepad.wButtons;
                const std::array<std::pair<WORD, Types::StandardId>, 14> buttonMap{
                    {{XINPUT_GAMEPAD_A, Standard::Gamepad::South},
                     {XINPUT_GAMEPAD_B, Standard::Gamepad::East},
                     {XINPUT_GAMEPAD_X, Standard::Gamepad::West},
                     {XINPUT_GAMEPAD_Y, Standard::Gamepad::North},
                     {XINPUT_GAMEPAD_DPAD_UP, Standard::Gamepad::DpadUp},
                     {XINPUT_GAMEPAD_DPAD_DOWN, Standard::Gamepad::DpadDown},
                     {XINPUT_GAMEPAD_DPAD_LEFT, Standard::Gamepad::DpadLeft},
                     {XINPUT_GAMEPAD_DPAD_RIGHT, Standard::Gamepad::DpadRight},
                     {XINPUT_GAMEPAD_LEFT_SHOULDER, Standard::Gamepad::LeftShoulder},
                     {XINPUT_GAMEPAD_RIGHT_SHOULDER, Standard::Gamepad::RightShoulder},
                     {XINPUT_GAMEPAD_BACK, Standard::Gamepad::Back},
                     {XINPUT_GAMEPAD_START, Standard::Gamepad::Start},
                     {XINPUT_GAMEPAD_LEFT_THUMB, Standard::Gamepad::LeftStickButton},
                     {XINPUT_GAMEPAD_RIGHT_THUMB, Standard::Gamepad::RightStickButton}}};
                for(const auto &[mask, standard] : buttonMap)
                {
                    if(ElementBinding *button = standardElement(*gamepad, standard); button != nullptr)
                    {
                        publish(state, button->id, mask == 0 ? false : (buttons & mask) != 0, false);
                    }
                }
                if(ElementBinding *leftStick = findRole(*gamepad, ElementRole::LeftStick); leftStick != nullptr)
                {
                    publish(state, leftStick->id, Types::Vector2F32{xInputAxis(packet.Gamepad.sThumbLX), xInputAxis(packet.Gamepad.sThumbLY)}, false);
                }
                if(ElementBinding *rightStick = findRole(*gamepad, ElementRole::RightStick); rightStick != nullptr)
                {
                    publish(
                        state,
                        rightStick->id,
                        Types::Vector2F32{xInputAxis(packet.Gamepad.sThumbRX), xInputAxis(packet.Gamepad.sThumbRY)},
                        false);
                }
                const std::array<std::pair<BYTE, Types::StandardId>, 2> triggerMap{
                    {{packet.Gamepad.bLeftTrigger, Standard::Gamepad::LeftTrigger}, {packet.Gamepad.bRightTrigger, Standard::Gamepad::RightTrigger}}};
                for(const auto &[value, standard] : triggerMap)
                {
                    if(ElementBinding *trigger = standardElement(*gamepad, standard); trigger != nullptr)
                    {
                        publish(state, trigger->id, xInputTrigger(value), false);
                    }
                }
            }
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            ++implementation.system->impl_->health.totalBackendFailures;
            implementation.system->impl_->pendingDiscontinuity = true;
            return status(ErrorCode::OutOfMemory);
        }
    }

    void Attachment::beforeCapture(System::Impl &, void *context) noexcept
    {
        auto *attachment = static_cast<Impl *>(context);
        if(attachment == nullptr || attachment->system == nullptr)
        {
            return;
        }
        (void)pollNative(*attachment);
    }

    void Attachment::afterCapture(System::Impl &, void *) noexcept {}

    void Attachment::closeSystem(System::Impl &, void *context) noexcept
    {
        auto *attachment = static_cast<Impl *>(context);
        if(attachment == nullptr)
        {
            return;
        }
        unregisterHidDeviceNotification(*attachment);
        unregisterRawInput(*attachment);
        releaseXInput(*attachment);
        Attachment::Impl *owner = attachment;
        processRegistrationOwner.compare_exchange_strong(owner, nullptr, std::memory_order_acq_rel);
        attachment->system = nullptr;
        attachment->windowHandle = nullptr;
        attachment->attached = false;
        attachment->devices.clear();
        attachment->pendingHighSurrogate = 0;
        attachment->rawInputBuffer.clear();
    }
} // namespace GameWIP::Input::Platform::Win32
