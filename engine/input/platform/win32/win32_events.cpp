/// @file win32_events.cpp
/// @brief Win32 observation publication, focus handling, and text translation.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/win32_keyboard_mapping.h"

#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <new>
#include <string>
#include <utility>

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// ------------------------------------------------------------
    /// Focus and value publication
    /// ------------------------------------------------------------

    void invalidateFocus(System::Impl &state, Attachment::Impl &attachment) noexcept
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
        System::Impl &state,
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

    [[nodiscard]] ElementBinding *standardElement(DeviceBinding &device, Types::StandardId standard) noexcept
    {
        return findElement(device, standard);
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

    void noteRawKeyboardTransition(Attachment::Impl &attachment, std::uint32_t usage, bool extended, bool pressed, std::uint32_t messageTime) noexcept
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
        System::Impl &state,
        Attachment::Impl &attachment,
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
        Attachment::Impl &attachment,
        const Attachment::Impl::KeyboardTransition &windowTransition) noexcept
    {
        for(Attachment::Impl::KeyboardTransition &rawTransition : attachment.rawKeyboardTransitions)
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

    [[nodiscard]] static bool hasKeyboardTransition(const System::Impl &state, const Attachment::Impl::KeyboardTransition &transition) noexcept
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

    void processWindowKeyboardTransitions(System::Impl &state, Attachment::Impl &attachment)
    {
        const std::size_t queuedCount = attachment.windowKeyboardTransitionCount;
        for(std::size_t index = 0; index < queuedCount; ++index)
        {
            Attachment::Impl::KeyboardTransition transition = attachment.windowKeyboardTransitions[attachment.windowKeyboardTransitionHead];
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
    /// XInput helpers
    /// ------------------------------------------------------------

    [[nodiscard]] float xInputTrigger(BYTE value) noexcept
    {
        return static_cast<float>(value) / 255.0F;
    }

    [[nodiscard]] float xInputAxis(SHORT value) noexcept
    {
        return value < 0 ? static_cast<float>(value) / 32768.0F : static_cast<float>(value) / 32767.0F;
    }

    void releaseXInput(Attachment::Impl &attachment) noexcept
    {
        if(attachment.xinputModule != nullptr)
        {
            FreeLibrary(attachment.xinputModule);
            attachment.xinputModule = nullptr;
        }
        attachment.xinputGetState = nullptr;
        attachment.xinputLoadAttempted = false;
        attachment.xinputFailureReported = false;
        attachment.xinputPackets.fill(0);
        attachment.xinputHasPackets.fill(false);
    }

    void loadXInput(Attachment::Impl &attachment) noexcept
    {
        if(attachment.xinputLoadAttempted)
        {
            return;
        }
        attachment.xinputLoadAttempted = true;
        constexpr std::array<const wchar_t *, 3> libraries{L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};

        for(const wchar_t *name : libraries)
        {
            HMODULE module = LoadLibraryW(name);
            if(module == nullptr)
            {
                continue;
            }

            const auto function = GameWIP::Base::Win32::loadProcedure<Attachment::Impl::XInputGetStateFunction>(module, "XInputGetState");
            if(function != nullptr)
            {
                attachment.xinputModule = module;
                attachment.xinputGetState = function;
                return;
            }
            FreeLibrary(module);
        }
    }

    /// ------------------------------------------------------------
    /// Raw report capture
    /// ------------------------------------------------------------

    void appendRawReport(
        System::Impl &state,
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

    /// ------------------------------------------------------------
    /// Text input
    /// ------------------------------------------------------------

    [[nodiscard]] static bool validScalar(char32_t scalar) noexcept
    {
        return scalar <= 0x10FFFF && (scalar < 0xD800 || scalar > 0xDFFF);
    }

    void appendTextUtf8(
        System::Impl &state,
        Attachment::Impl &attachment,
        std::string_view utf8,
        Types::EventKind kind,
        std::size_t selectionStartBytes,
        std::size_t selectionLengthBytes,
        bool canceled) noexcept
    {
        if(!state.textEnabled)
        {
            return;
        }

        // Validate the UTF-8 text before creating an event.
        if(GameWIP::Unicode::Utf8::validate(utf8).outcome != GameWIP::Unicode::Types::ValidationOutcome::Valid)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        // Enforce the pending-text limit for committed input.
        const bool committed = kind == Types::EventKind::TextCommitted;
        if(committed && (state.pendingTextUtf8.size() > state.configuration.maximumPendingTextBytes ||
                         utf8.size() > state.configuration.maximumPendingTextBytes - state.pendingTextUtf8.size()))
        {
            state.pendingDiscontinuity = true;
            return;
        }

        // Publish the event and retain committed text for the session.
        try
        {
            Types::Event event{
                .timestamp = now(),
                .kind = kind,
                .cause = Types::EventCause::Reported,
                .data =
                    Types::TextEvent{
                        .surface = Types::SurfaceId{surfaceValue(attachment.windowHandle)},
                        .utf8 = std::string(utf8),
                        .selectionStartBytes = selectionStartBytes,
                        .selectionLengthBytes = selectionLengthBytes,
                        .canceled = canceled,
                    },
            };
            const std::uint64_t droppedEvents = state.pendingDroppedEvents;
            state.appendEvent(std::move(event));

            if(state.pendingDroppedEvents != droppedEvents)
            {
                return;
            }

            if(committed)
            {
                state.pendingTextUtf8.append(utf8);
            }
        }
        catch(const std::bad_alloc &)
        {
            state.pendingDiscontinuity = true;
        }
    }

    void appendText(System::Impl &state, Attachment::Impl &attachment, char32_t scalar) noexcept
    {
        // Reject invalid Unicode scalars before encoding.
        if(!validScalar(scalar))
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        // Encode the scalar and reject conversion failures.
        const auto encoded = GameWIP::Unicode::Utf8::encodeScalar(scalar);
        if(encoded.outcome != GameWIP::Unicode::Types::EncodeOutcome::Encoded)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        // Publish the encoded character as committed text.
        appendTextUtf8(state, attachment, std::string_view(encoded.bytes.data(), encoded.byteCount), Types::EventKind::TextCommitted);
    }

    void appendImeText(
        System::Impl &state,
        Attachment::Impl &attachment,
        HIMC context,
        DWORD compositionFlag,
        Types::EventKind kind,
        LONG cursorPosition) noexcept
    {
        // Query and validate the native UTF-16 composition size.
        const LONG byteCount = ImmGetCompositionStringW(context, compositionFlag, nullptr, 0);
        if(byteCount < 0 || (byteCount % static_cast<LONG>(sizeof(wchar_t))) != 0)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        try
        {
            // Read and validate the composition code units.
            const std::size_t codeUnitCount = static_cast<std::size_t>(byteCount) / sizeof(wchar_t);
            std::vector<char16_t> codeUnits(codeUnitCount);
            if(byteCount != 0 && ImmGetCompositionStringW(context, compositionFlag, codeUnits.data(), static_cast<DWORD>(byteCount)) != byteCount)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                return;
            }

            const std::span<const char16_t> text(codeUnits.data(), codeUnits.size());
            if(GameWIP::Unicode::Utf16::validate(text).outcome != GameWIP::Unicode::Types::ValidationOutcome::Valid)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                return;
            }

            const auto measurement = GameWIP::Unicode::Utf16::measureToUtf8(text);
            if(measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                return;
            }

            // Convert the composition to UTF-8 and map its selection offset.
            std::string utf8(measurement.requiredBytes, '\0');
            const auto conversion = GameWIP::Unicode::Utf16::convertToUtf8(text, std::span<char>(utf8.data(), utf8.size()));
            if(conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                return;
            }
            std::size_t selectionStartBytes = 0;
            if(cursorPosition > 0)
            {
                const std::size_t cursor = std::min<std::size_t>(static_cast<std::size_t>(cursorPosition), codeUnits.size());
                const auto prefix = GameWIP::Unicode::Utf16::measureToUtf8(text.first(cursor));
                if(prefix.outcome == GameWIP::Unicode::Types::MeasureOutcome::Measured)
                {
                    selectionStartBytes = prefix.requiredBytes;
                }
            }

            // Publish the validated composition text.
            appendTextUtf8(state, attachment, utf8, kind, selectionStartBytes);
        }
        catch(const std::bad_alloc &)
        {
            state.pendingDiscontinuity = true;
        }
    }

    void handleUtf16(System::Impl &state, Attachment::Impl &attachment, char16_t codeUnit) noexcept
    {
        const bool high = codeUnit >= 0xD800 && codeUnit <= 0xDBFF;
        const bool low = codeUnit >= 0xDC00 && codeUnit <= 0xDFFF;

        // Keep a high surrogate until its matching low surrogate arrives.
        if(high)
        {
            if(attachment.pendingHighSurrogate != 0)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
            }
            attachment.pendingHighSurrogate = codeUnit;
            return;
        }

        // Combine a valid surrogate pair into one Unicode scalar.
        if(low)
        {
            if(attachment.pendingHighSurrogate == 0)
            {
                ++state.health.totalInvalidReports;
                state.pendingDiscontinuity = true;
                return;
            }
            const char32_t scalar = static_cast<char32_t>(
                0x10000U + ((static_cast<std::uint32_t>(attachment.pendingHighSurrogate) - 0xD800U) << 10U) +
                (static_cast<std::uint32_t>(codeUnit) - 0xDC00U));
            attachment.pendingHighSurrogate = 0;
            appendText(state, attachment, scalar);
            return;
        }

        // Reject an unmatched high surrogate before publishing this code unit.
        if(attachment.pendingHighSurrogate != 0)
        {
            attachment.pendingHighSurrogate = 0;
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
        }

        appendText(state, attachment, static_cast<char32_t>(codeUnit));
    }

} // namespace GameWIP::Input::Platform::Win32::Detail
