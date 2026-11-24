/// @file win32_events.cpp
/// @brief Win32 value publication and keyboard transition handling.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/win32_keyboard_mapping.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Focus and value publication
    /// ------------------------------------------------------------

    void invalidateFocus(SystemState &state, AttachmentState &attachment) noexcept
    {
        for(std::size_t bindingIndex = 0; bindingIndex < attachment.devices.size(); ++bindingIndex)
        {
            DeviceBinding &device = attachment.devices[bindingIndex];
            if(!device.connected)
            {
                continue;
            }

            const bool alreadyProcessed = std::any_of(
                attachment.devices.begin(),
                attachment.devices.begin() + static_cast<std::ptrdiff_t>(bindingIndex),
                [&device](const DeviceBinding &candidate)
                {
                    return candidate.connected && candidate.id == device.id;
                });
            if(alreadyProcessed)
            {
                continue;
            }

            const std::size_t deviceIndex = state.findDeviceIndex(device.id);
            if(deviceIndex == state.devices.size())
            {
                continue;
            }

            for(const Types::ElementInfo &element : state.devices[deviceIndex].elements)
            {
                if(Types::ElementState *elementState = findState(state, element.id);
                   elementState != nullptr && elementState->validity == Types::ValueValidity::Valid)
                {
                    elementState->validity = Types::ValueValidity::Unavailable;
                    state.appendEvent({
                        .timestamp = now(),
                        .kind = Types::EventKind::ElementInvalidated,
                        .cause = Types::EventCause::FocusLost,
                        .data =
                            Types::ElementEvent{
                                .id = element.id,
                                .value = elementState->value,
                                .previousValue = elementState->value,
                                .previousKnown = false,
                            },
                    });
                }
            }
        }

        attachment.pendingHighSurrogate = 0;
        attachment.windowKeyboardTransitionHead = 0;
        attachment.windowKeyboardTransitionCount = 0;
        attachment.rawKeyboardTransitions.fill({});
        attachment.rawKeyboardTransitionNext = 0;
    }

    [[nodiscard]] static Types::Value zeroFor(const Types::ElementInfo &info) noexcept
    {
        return Internal::zeroValue(info.kind);
    }

    void publish(
        SystemState &state,
        Types::ElementId id,
        const Types::Value &value,
        bool relative,
        Types::Timestamp timestamp,
        Types::EventCause cause) noexcept
    {
        Types::ElementState *elementState = findState(state, id);
        Types::ElementInfo *element = findElement(state, id);

        if(elementState == nullptr || element == nullptr)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        if(Internal::valueKind(value) != element->kind)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        const bool previousKnown = elementState->validity == Types::ValueValidity::Valid;
        const Types::Value previous = elementState->value;
        Types::Value next = value;

        if(relative)
        {
            next = previousKnown ? previous : zeroFor(*element);
            if(!Internal::addRelative(next, value))
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                elementState->validity = Types::ValueValidity::Stale;
                return;
            }
        }

        if(previousKnown && !relative && previous == next)
        {
            ++elementState->repeatCount;
            elementState->lastObservation = timestamp.receivedAt == Types::Clock::time_point{} ? now() : timestamp;
            return;
        }

        if(const bool *nextBool = std::get_if<bool>(&next); nextBool != nullptr)
        {
            const bool *previousBool = previousKnown ? std::get_if<bool>(&previous) : nullptr;
            if(*nextBool && (previousBool == nullptr || !*previousBool))
            {
                ++elementState->becameTrueCount;
            }
            else if(previousBool != nullptr && *previousBool && !*nextBool)
            {
                ++elementState->becameFalseCount;
            }
        }

        elementState->value = next;
        elementState->validity = Types::ValueValidity::Valid;
        elementState->lastObservation = timestamp.receivedAt == Types::Clock::time_point{} ? now() : timestamp;
        appendElementEvent(state, id, relative ? value : next, previous, previousKnown, cause);
    }

    /// ------------------------------------------------------------
    /// Keyboard mapping and fallback
    /// ------------------------------------------------------------

    [[nodiscard]] static std::uint32_t keyboardUsage(unsigned int scanCode, bool extended, bool extendedPause, unsigned int virtualKey) noexcept
    {
        if(virtualKey == VK_PAUSE || extendedPause)
        {
            return 0x48;
        }

        const std::uint32_t usage = keyboardUsageForScanCode(scanCode, extended, extendedPause);
        if(usage != 0 || extended || scanCode < 0x59)
        {
            return usage;
        }

        if(virtualKey != 0)
        {
            const UINT mapped = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
            if(mapped != 0 && mapped != scanCode)
            {
                return keyboardUsage(mapped & 0xFFU, (mapped & 0xFF00U) == 0xE000U, false, virtualKey);
            }
        }
        return 0;
    }

    [[nodiscard]] std::uint32_t rawKeyboardUsage(const RAWKEYBOARD &keyboard) noexcept
    {
        if(keyboard.MakeCode != 0)
        {
            return keyboardUsage(
                static_cast<unsigned int>(keyboard.MakeCode) & 0xFFU,
                (keyboard.Flags & RI_KEY_E0) != 0,
                (keyboard.Flags & RI_KEY_E1) != 0,
                keyboard.VKey);
        }

        const UINT mapped = MapVirtualKeyW(keyboard.VKey, MAPVK_VK_TO_VSC_EX);
        if(mapped == 0)
        {
            return 0;
        }
        return keyboardUsage(mapped & 0xFFU, (mapped & 0xFF00U) == 0xE000U, false, keyboard.VKey);
    }

    void noteRawKeyboardTransition(AttachmentState &attachment, std::uint32_t usage, bool extended, bool pressed, std::uint32_t messageTime) noexcept
    {
        if(usage == 0)
        {
            return;
        }

        auto &transition = attachment.rawKeyboardTransitions[attachment.rawKeyboardTransitionNext];
        transition = {
            .usage = usage,
            .messageTime = messageTime,
            .extended = extended,
            .pressed = pressed,
            .valid = true,
        };
        attachment.rawKeyboardTransitionNext = (attachment.rawKeyboardTransitionNext + 1) % attachment.rawKeyboardTransitions.size();
    }

    void queueWindowKeyboardTransition(
        SystemState &state,
        AttachmentState &attachment,
        std::uint32_t usage,
        bool extended,
        bool pressed,
        std::uint32_t messageTime) noexcept
    {
        if(usage == 0 || attachment.system == nullptr)
        {
            return;
        }

        if(attachment.windowKeyboardTransitionCount == attachment.windowKeyboardTransitions.size())
        {
            ++state.health.totalDroppedEvents;
            ++state.pendingDroppedEvents;
            state.pendingDiscontinuity = true;
            attachment.windowKeyboardTransitionOverflow = true;
            return;
        }

        const std::size_t index =
            (attachment.windowKeyboardTransitionHead + attachment.windowKeyboardTransitionCount) % attachment.windowKeyboardTransitions.size();
        attachment.windowKeyboardTransitions[index] = {
            .usage = usage,
            .messageTime = messageTime,
            .captureDelay = 1,
            .extended = extended,
            .pressed = pressed,
            .valid = true,
        };
        ++attachment.windowKeyboardTransitionCount;
    }

    [[nodiscard]] static bool closeMessageTime(std::uint32_t left, std::uint32_t right) noexcept
    {
        constexpr std::uint32_t kDeduplicationWindowMilliseconds = 50;
        return std::min(left - right, right - left) <= kDeduplicationWindowMilliseconds;
    }

    [[nodiscard]] static bool matchesRawKeyboardTransition(
        AttachmentState &attachment,
        const AttachmentState::KeyboardTransition &windowTransition) noexcept
    {
        for(AttachmentState::KeyboardTransition &rawTransition : attachment.rawKeyboardTransitions)
        {
            if(!rawTransition.valid || rawTransition.matched || rawTransition.usage != windowTransition.usage ||
               rawTransition.extended != windowTransition.extended || rawTransition.pressed != windowTransition.pressed ||
               !closeMessageTime(rawTransition.messageTime, windowTransition.messageTime))
            {
                continue;
            }

            rawTransition.matched = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] static bool hasKeyboardTransition(const SystemState &state, const AttachmentState::KeyboardTransition &transition) noexcept
    {
        for(const Types::Event &event : state.pendingEvents)
        {
            if(event.kind != Types::EventKind::ElementChanged)
            {
                continue;
            }

            const auto *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
            const Types::ElementInfo *element = elementEvent == nullptr ? nullptr : state.findElement(elementEvent->id);
            const bool *value = elementEvent == nullptr ? nullptr : std::get_if<bool>(&elementEvent->value);
            if(element != nullptr && value != nullptr && *value == transition.pressed && element->identity.standard.has_value() &&
               element->identity.standard->domain == Types::StandardDomain::Keyboard && element->identity.standard->code == transition.usage)
            {
                return true;
            }
        }
        return false;
    }

    void processWindowKeyboardTransitions(SystemState &state, AttachmentState &attachment)
    {
        const std::size_t queuedCount = attachment.windowKeyboardTransitionCount;
        for(std::size_t index = 0; index < queuedCount; ++index)
        {
            AttachmentState::KeyboardTransition transition = attachment.windowKeyboardTransitions[attachment.windowKeyboardTransitionHead];
            attachment.windowKeyboardTransitionHead = (attachment.windowKeyboardTransitionHead + 1) % attachment.windowKeyboardTransitions.size();
            --attachment.windowKeyboardTransitionCount;

            if(matchesRawKeyboardTransition(attachment, transition) || hasKeyboardTransition(state, transition))
            {
                continue;
            }

            if(transition.captureDelay != 0)
            {
                --transition.captureDelay;
                const std::size_t tail = (attachment.windowKeyboardTransitionHead + attachment.windowKeyboardTransitionCount) %
                                         attachment.windowKeyboardTransitions.size();
                attachment.windowKeyboardTransitions[tail] = transition;
                ++attachment.windowKeyboardTransitionCount;
                continue;
            }

            DeviceBinding *keyboard = findDevice(attachment, DeviceRole::MessageKeyboard, 0);
            if(keyboard == nullptr)
            {
                addDevice(
                    state,
                    attachment,
                    DeviceRole::MessageKeyboard,
                    0,
                    "Window Message Keyboard",
                    {},
                    true,
                    Types::SourceInfo{
                        .backend = Types::BackendKind::Win32WindowMessage,
                        .backendName = "Win32 Window Messages",
                        .active = true,
                    });
                keyboard = findDevice(attachment, DeviceRole::MessageKeyboard, 0);
            }

            if(keyboard == nullptr)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                continue;
            }

            const Types::StandardId standard{Types::StandardDomain::Keyboard, transition.usage};
            ElementBinding *element = standardElement(*keyboard, standard);
            if(element == nullptr)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                continue;
            }

            publish(state, element->id, transition.pressed, false);
        }
    }

    /// ------------------------------------------------------------
    /// Raw report capture
    /// ------------------------------------------------------------

    void appendRawReport(
        SystemState &state,
        const Types::Configuration &configuration,
        Types::DeviceId device,
        Types::BackendKind backend,
        std::span<const std::byte> bytes) noexcept
    {
        if(!configuration.captureRawReports)
        {
            return;
        }

        if(state.pendingRawReports.size() >= configuration.rawReportCapacity)
        {
            state.pendingDiscontinuity = true;
            return;
        }

        const std::size_t count = std::min(bytes.size(), configuration.maximumRawReportBytes);
        try
        {
            Types::RawReport report{
                .device = device,
                .backend = backend,
                .timestamp = now(),
            };
            report.bytes.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(count));
            state.pendingRawReports.push_back(std::move(report));

            if(count != bytes.size())
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
            }
        }
        catch(const std::bad_alloc &)
        {
            state.pendingDiscontinuity = true;
        }
    }

} // namespace GameWIP::Input::Platform::Win32
