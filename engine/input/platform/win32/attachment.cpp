/// @file attachment.cpp
/// @brief Implements the Win32 Input attachment lifecycle and provider polling.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/internal/results.h"
#include "input/win32.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <format>
#include <new>
#include <string>
#include <utility>

namespace GameWIP::Input::Platform::Win32
{
    namespace
    {
        std::atomic<AttachmentState *> processRegistrationOwner{nullptr};
    }

    /// ------------------------------------------------------------
    /// Backend selection and status
    /// ------------------------------------------------------------

    [[nodiscard]] static std::string nativeCodeDetail(
        std::int64_t nativeCode,
        GameWIP::Status::Types::NativeCodeDomain domain = GameWIP::Status::Types::NativeCodeDomain::Win32Error)
    {
        return std::format("native={} 0x{:08X}", GameWIP::Status::Types::nativeCodeDomainName(domain), static_cast<std::uint32_t>(nativeCode));
    }

    [[nodiscard]] static std::string nativeCodeDetail(const GameWIP::Status::Types::Status &status)
    {
        const GameWIP::Status::Types::Diagnostic *diagnostic = status.diagnostic();
        if(diagnostic == nullptr || !diagnostic->nativeCode.has_value())
        {
            return {};
        }

        return nativeCodeDetail(diagnostic->nativeCode->value, diagnostic->nativeCode->domain);
    }

    bool backendSelected(const Types::Configuration &configuration, Types::BackendKind backend) noexcept
    {
        if(!configuration.enabledBackends.has_value())
        {
            return true;
        }

        const auto &enabledBackends = *configuration.enabledBackends;
        return std::find(enabledBackends.begin(), enabledBackends.end(), backend) != enabledBackends.end();
    }

    void setBackendStatus(AttachmentState &attachment, Types::BackendKind backend, Types::BackendStatus statusValue, std::string_view detail)
    {
        const auto entry = std::find_if(
            attachment.backendStatuses.begin(),
            attachment.backendStatuses.end(),
            [backend](const Types::BackendInfo &candidate)
            {
                return candidate.backend == backend;
            });
        if(entry != attachment.backendStatuses.end())
        {
            entry->status = statusValue;
            entry->detail.assign(detail);
        }
    }

    /// ------------------------------------------------------------
    /// Attachment lifecycle
    /// ------------------------------------------------------------

    Attachment::Attachment() noexcept
        : impl_(std::unique_ptr<AttachmentState>(new (std::nothrow) AttachmentState{}))
    {
    }

    Attachment::~Attachment() noexcept
    {
        if(impl_ != nullptr)
        {
            (void)detach();
        }
    }

    GameWIP::Status::Types::Result<void> Attachment::attach(System &system, void *windowHandle) noexcept
    {
        return GameWIP::Input::Internal::result(attachStatus(system, windowHandle));
    }

    GameWIP::Status::Types::Result<void> Attachment::detach() noexcept
    {
        return GameWIP::Input::Internal::result(detachStatus());
    }

    GameWIP::Status::Types::Result<std::vector<Types::BackendInfo>> Attachment::backends() const noexcept
    {
        std::vector<Types::BackendInfo> result;
        const GameWIP::Status::Types::Status operationStatus = backendsStatus(result);
        if(!operationStatus.ok())
        {
            return GameWIP::Input::Internal::failedResult<std::vector<Types::BackendInfo>>(operationStatus);
        }
        return GameWIP::Status::Types::Result<std::vector<Types::BackendInfo>>::success(std::move(result));
    }

    GameWIP::Status::Types::Result<void> Attachment::poll() noexcept
    {
        return GameWIP::Input::Internal::result(pollStatus());
    }

    GameWIP::Status::Types::Status Attachment::attachStatus(System &system, void *windowHandle) noexcept
    {
        // Validate the attachment target and Input session.
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        if(impl_->attached)
        {
            return impl_->system == &system ? status(Code::AlreadyOpen) : status(Code::ResourceBusy);
        }

        if(windowHandle == nullptr)
        {
            return status(Code::InvalidArgument);
        }

        if(!system.isOpen())
        {
            return status(Code::NotOpen);
        }

        if(!system.ownedByCurrentThread())
        {
            return status(Code::ResourceBusy);
        }

        if(system.impl_->nativeBeforeCapture != nullptr)
        {
            return status(Code::AlreadyOpen);
        }

        // Claim the process-wide native registration slot.
        AttachmentState *expected = nullptr;
        if(!processRegistrationOwner.compare_exchange_strong(expected, impl_.get(), std::memory_order_acq_rel))
        {
            return status(Code::ResourceBusy);
        }

        try
        {
            // Bind the attachment to the session and window.
            impl_->system = &system;
            impl_->windowHandle = windowHandle;
            impl_->attached = true;

            // Reset backend status before initialization.
            for(Types::BackendInfo &backend : impl_->backendStatuses)
            {
                backend.status = backendSelected(system.impl_->configuration, backend.backend) ? Types::BackendStatus::Unavailable
                                                                                               : Types::BackendStatus::Disabled;
                backend.detail.clear();
            }

            // Apply the configured backend selection.
            impl_->rawInputEnabled = backendSelected(system.impl_->configuration, Types::BackendKind::Win32RawInput);
            impl_->hidEnabled = backendSelected(system.impl_->configuration, Types::BackendKind::Win32Hid);
            impl_->xinputEnabled = backendSelected(system.impl_->configuration, Types::BackendKind::XInput);
            impl_->gameInputEnabled = backendSelected(system.impl_->configuration, Types::BackendKind::GameInput);
            impl_->directInputEnabled = backendSelected(system.impl_->configuration, Types::BackendKind::DirectInput);
            impl_->windowMessagesEnabled = backendSelected(system.impl_->configuration, Types::BackendKind::Win32WindowMessage);

            // Register Raw Input, HID, and XInput sources.
            if(impl_->rawInputEnabled)
            {
                const GameWIP::Status::Types::Status rawInputStatus =
                    registerRawInputCollection(*impl_, HID_USAGE_PAGE_GENERIC, 0, RIDEV_PAGEONLY | RIDEV_DEVNOTIFY);
                if(rawInputStatus.ok())
                {
                    setBackendStatus(*impl_, Types::BackendKind::Win32RawInput, Types::BackendStatus::Enabled);
                }
                else
                {
                    impl_->rawInputEnabled = false;
                    ++system.impl_->health.totalBackendFailures;
                    setBackendStatus(*impl_, Types::BackendKind::Win32RawInput, Types::BackendStatus::Failed, nativeCodeDetail(rawInputStatus));
                }
            }

            if(impl_->hidEnabled)
            {
                if(!registerHidDeviceNotification(*impl_))
                {
                    const DWORD error = GetLastError();
                    ++system.impl_->health.totalBackendFailures;
                    setBackendStatus(*impl_, Types::BackendKind::Win32Hid, Types::BackendStatus::Failed, nativeCodeDetail(error));
                    impl_->hidEnabled = false;
                }
                else
                {
                    const GameWIP::Status::Types::Status hidCollectionsStatus = registerHidCollections(*impl_);
                    if(hidCollectionsStatus.ok())
                    {
                        setBackendStatus(*impl_, Types::BackendKind::Win32Hid, Types::BackendStatus::Enabled);
                    }
                    else
                    {
                        ++system.impl_->health.totalBackendFailures;
                        setBackendStatus(*impl_, Types::BackendKind::Win32Hid, Types::BackendStatus::Failed, nativeCodeDetail(hidCollectionsStatus));
                        unregisterHidDeviceNotification(*impl_);
                        impl_->hidEnabled = false;
                    }
                }
            }

            if(impl_->xinputEnabled)
            {
                loadXInput(*impl_);
                if(impl_->xinputGetState != nullptr)
                {
                    setBackendStatus(*impl_, Types::BackendKind::XInput, Types::BackendStatus::Enabled);
                }
                else
                {
                    setBackendStatus(*impl_, Types::BackendKind::XInput, Types::BackendStatus::Unavailable, "XInput runtime not found");
                    impl_->xinputEnabled = false;
                }
            }

            // Connect the attachment to the capture lifecycle.
            system.impl_->nativeBeforeCapture = &Attachment::beforeCapture;
            system.impl_->nativeClose = &Attachment::closeSystem;
            system.impl_->nativeContext = impl_.get();

            // Enumerate native devices and add fallback sources.
            const bool enumerated = (impl_->rawInputEnabled || impl_->hidEnabled) ? enumerateNativeDevices(*system.impl_, *impl_) : true;
            if(!enumerated)
            {
                ++system.impl_->health.totalBackendFailures;
            }

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

            if(impl_->rawInputEnabled && (!enumerated || !hasRole(DeviceRole::Keyboard)))
            {
                addDevice(*system.impl_, *impl_, DeviceRole::Keyboard, 0, "Keyboard", {}, true);
            }

            if(impl_->rawInputEnabled && (!enumerated || !hasRole(DeviceRole::Mouse)))
            {
                addDevice(*system.impl_, *impl_, DeviceRole::Mouse, 0, "Mouse", {}, true);
            }

            if(impl_->windowMessagesEnabled)
            {
                setBackendStatus(*impl_, Types::BackendKind::Win32WindowMessage, Types::BackendStatus::Enabled);
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
            }

            // Initialize the optional polling backends.
            if(impl_->directInputEnabled)
            {
                const GameWIP::Status::Types::Status directInputStatus = initializeDirectInput(*impl_);
                if(directInputStatus.ok())
                {
                    setBackendStatus(*impl_, Types::BackendKind::DirectInput, Types::BackendStatus::Enabled);
                    const GameWIP::Status::Types::Status enumerationStatus = enumerateDirectInputDevices(*impl_, *system.impl_);
                    if(!enumerationStatus.ok())
                    {
                        ++system.impl_->health.totalBackendFailures;
                        setBackendStatus(*impl_, Types::BackendKind::DirectInput, Types::BackendStatus::Failed, nativeCodeDetail(enumerationStatus));
                    }
                }
                else
                {
                    if(directInputStatus.code() != Code::Unsupported)
                    {
                        ++system.impl_->health.totalBackendFailures;
                    }
                    const Types::BackendStatus backendStatus =
                        directInputStatus.code() == Code::Unsupported ? Types::BackendStatus::Unavailable : Types::BackendStatus::Failed;
                    setBackendStatus(
                        *impl_,
                        Types::BackendKind::DirectInput,
                        backendStatus,
                        directInputStatus.code() == Code::Unsupported ? "DirectInput runtime not found" : nativeCodeDetail(directInputStatus));
                    impl_->directInputEnabled = false;
                }
            }

            if(impl_->gameInputEnabled)
            {
                const GameWIP::Status::Types::Status gameInputStatus = initializeGameInput(*impl_, *system.impl_);
                if(gameInputStatus.ok())
                {
                    setBackendStatus(*impl_, Types::BackendKind::GameInput, Types::BackendStatus::Enabled);
                }
                else
                {
                    if(gameInputStatus.code() != Code::Unsupported)
                    {
                        ++system.impl_->health.totalBackendFailures;
                    }
                    const Types::BackendStatus backendStatus =
                        gameInputStatus.code() == Code::Unsupported ? Types::BackendStatus::Unavailable : Types::BackendStatus::Failed;
                    setBackendStatus(
                        *impl_,
                        Types::BackendKind::GameInput,
                        backendStatus,
                        gameInputStatus.code() == Code::Unsupported ? "GameInput runtime not found" : nativeCodeDetail(gameInputStatus));
                    impl_->gameInputEnabled = false;
                }
            }

            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            // Release providers and registrations created by this attempt.
            unregisterHidDeviceNotification(*impl_);
            unregisterRawInput(*impl_);
            shutdownGameInput(*impl_);
            shutdownDirectInput(*impl_);
            releaseXInput(*impl_);
            processRegistrationOwner.store(nullptr, std::memory_order_release);

            if(system.impl_->nativeContext == impl_.get())
            {
                // Remove callbacks installed by the partial attachment.
                system.impl_->nativeBeforeCapture = nullptr;
                system.impl_->nativeClose = nullptr;
                system.impl_->nativeContext = nullptr;
            }

            // Reset attachment state before returning the allocation error.
            impl_->system = nullptr;
            impl_->windowHandle = nullptr;
            impl_->attached = false;
            impl_->devices.clear();
            impl_->pendingHighSurrogate = 0;
            impl_->rawInputBuffer.clear();
            return status(Code::OutOfMemory);
        }
    }

    GameWIP::Status::Types::Status Attachment::detachStatus() noexcept
    {
        // Ignore repeated detach requests.
        if(impl_ == nullptr || !impl_->attached)
        {
            return GameWIP::Status::Types::Status{};
        }

        if(impl_->system != nullptr && impl_->system->isOpen())
        {
            const GameWIP::Status::Types::Status ownerStatus = checkAttached(*impl_);
            if(!ownerStatus.ok())
            {
                return ownerStatus;
            }

            // Publish disconnects and remove capture callbacks.
            for(DeviceBinding &device : impl_->devices)
            {
                invalidateDevice(*impl_->system->impl_, *impl_, device, Types::EventCause::Disconnected);
            }

            if(impl_->system->impl_->nativeContext == impl_.get())
            {
                impl_->system->impl_->nativeBeforeCapture = nullptr;
                impl_->system->impl_->nativeClose = nullptr;
                impl_->system->impl_->nativeContext = nullptr;
            }
        }

        // Stop providers and release native registrations.
        unregisterHidDeviceNotification(*impl_);
        unregisterRawInput(*impl_);
        releaseXInput(*impl_);
        shutdownGameInput(*impl_);
        shutdownDirectInput(*impl_);

        AttachmentState *owner = impl_.get();
        processRegistrationOwner.compare_exchange_strong(owner, nullptr, std::memory_order_acq_rel);

        // Clear attachment state.
        impl_->system = nullptr;
        impl_->windowHandle = nullptr;
        impl_->attached = false;
        impl_->devices.clear();
        impl_->pendingHighSurrogate = 0;
        impl_->rawInputBuffer.clear();

        return GameWIP::Status::Types::Status{};
    }

    bool Attachment::isAttached() const noexcept
    {
        return impl_ != nullptr && impl_->attached && impl_->system != nullptr && impl_->system->isOpen();
    }

    GameWIP::Status::Types::Status Attachment::backendsStatus(std::vector<Types::BackendInfo> &outBackends) const noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status ownerStatus = checkAttached(*impl_);
        if(!ownerStatus.ok())
        {
            return ownerStatus;
        }

        try
        {
            outBackends.assign(impl_->backendStatuses.begin(), impl_->backendStatuses.end());
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return status(Code::OutOfMemory);
        }
    }

    Types::SurfaceId Attachment::surfaceId() const noexcept
    {
        return isAttached() ? Types::SurfaceId{surfaceValue(impl_->windowHandle)} : Types::SurfaceId{};
    }

    /// ------------------------------------------------------------
    /// Provider polling
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status Attachment::pollStatus() noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status ownerStatus = checkAttached(*impl_);
        return ownerStatus.ok() ? pollNative(*impl_) : ownerStatus;
    }

    GameWIP::Status::Types::Status Attachment::pollNative(AttachmentState &implementation) noexcept
    {
        try
        {
            SystemState &state = *implementation.system->impl_;
            GameWIP::Status::Types::Status pollStatus;

            // Poll XInput and publish changed controller state.
            if(implementation.xinputEnabled && implementation.xinputGetState != nullptr)
            {
                for(DWORD slot = 0; slot < 4; ++slot)
                {
                    // Read the slot state and find its current device.
                    XINPUT_STATE packet{};
                    const DWORD result = implementation.xinputGetState(slot, &packet);
                    DeviceBinding *gamepad = findDevice(implementation, DeviceRole::Gamepad, slot);

                    // Disconnect devices that no longer respond.
                    if(result != ERROR_SUCCESS)
                    {
                        if(gamepad != nullptr)
                        {
                            invalidateDevice(state, implementation, *gamepad, Types::EventCause::Disconnected);
                        }
                        implementation.xinputHasPackets[slot] = false;
                        continue;
                    }

                    // Create or reconnect the gamepad descriptor.
                    if(gamepad == nullptr)
                    {
                        addDevice(state, implementation, DeviceRole::Gamepad, slot, "XInput Controller " + std::to_string(slot + 1), {}, true);
                        gamepad = findDevice(implementation, DeviceRole::Gamepad, slot);
                    }
                    else if(!gamepad->connected)
                    {
                        reconnectDevice(state, *gamepad);
                    }

                    // Skip missing bindings and unchanged packets.
                    if(gamepad == nullptr)
                    {
                        continue;
                    }

                    if(implementation.xinputHasPackets[slot] && implementation.xinputPackets[slot] == packet.dwPacketNumber)
                    {
                        continue;
                    }

                    // Save the packet number before publishing its controls.
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

                    // Publish button states.
                    for(const auto &[mask, standard] : buttonMap)
                    {
                        if(ElementBinding *button = standardElement(*gamepad, standard); button != nullptr)
                        {
                            publish(state, button->id, (buttons & mask) != 0, false);
                        }
                    }

                    // Publish stick axes.
                    if(ElementBinding *leftStick = findRole(*gamepad, ElementRole::LeftStick); leftStick != nullptr)
                    {
                        publish(
                            state,
                            leftStick->id,
                            Types::Vector2F32{xInputAxis(packet.Gamepad.sThumbLX), xInputAxis(packet.Gamepad.sThumbLY)},
                            false);
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
                        {{packet.Gamepad.bLeftTrigger, Standard::Gamepad::LeftTrigger},
                         {packet.Gamepad.bRightTrigger, Standard::Gamepad::RightTrigger}}};

                    // Publish trigger axes.
                    for(const auto &[value, standard] : triggerMap)
                    {
                        if(ElementBinding *trigger = standardElement(*gamepad, standard); trigger != nullptr)
                        {
                            publish(state, trigger->id, xInputTrigger(value), false);
                        }
                    }
                }
            }

            // Poll the other selected backends and keyboard fallback.
            if(implementation.directInputEnabled)
            {
                const GameWIP::Status::Types::Status directInputStatus = pollDirectInput(implementation, state);
                if(!directInputStatus.ok() && pollStatus.ok())
                {
                    pollStatus = directInputStatus;
                }
            }

            if(implementation.gameInputEnabled)
            {
                const GameWIP::Status::Types::Status gameInputStatus = pollGameInput(implementation, state);
                if(!gameInputStatus.ok() && pollStatus.ok())
                {
                    pollStatus = gameInputStatus;
                }
            }

            if(implementation.windowMessagesEnabled)
            {
                processWindowKeyboardTransitions(state, implementation);
            }

            return pollStatus;
        }
        catch(const std::bad_alloc &)
        {
            // Mark this poll as incomplete for the next captured frame.
            ++implementation.system->impl_->health.totalBackendFailures;
            implementation.system->impl_->pendingDiscontinuity = true;
            return status(Code::OutOfMemory);
        }
    }

    /// ------------------------------------------------------------
    /// Input session callbacks
    /// ------------------------------------------------------------

    void Attachment::beforeCapture(SystemState &, void *context) noexcept
    {
        auto *attachment = static_cast<AttachmentState *>(context);
        if(attachment == nullptr || attachment->system == nullptr)
        {
            return;
        }
        (void)pollNative(*attachment);
    }

    void Attachment::closeSystem(SystemState &, void *context) noexcept
    {
        auto *attachment = static_cast<AttachmentState *>(context);
        if(attachment == nullptr)
        {
            return;
        }

        // Stop providers and release native registrations.
        unregisterHidDeviceNotification(*attachment);
        unregisterRawInput(*attachment);
        releaseXInput(*attachment);
        shutdownGameInput(*attachment);
        shutdownDirectInput(*attachment);

        AttachmentState *owner = attachment;
        processRegistrationOwner.compare_exchange_strong(owner, nullptr, std::memory_order_acq_rel);

        // Clear attachment state.
        attachment->system = nullptr;
        attachment->windowHandle = nullptr;
        attachment->attached = false;
        attachment->devices.clear();
        attachment->pendingHighSurrogate = 0;
        attachment->rawInputBuffer.clear();
    }
} // namespace GameWIP::Input::Platform::Win32
