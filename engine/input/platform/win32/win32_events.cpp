/// @file win32_events.cpp
/// @brief Win32 observation publication, focus handling, and text translation.

#include "input/platform/win32/internal/win32_attachment_state.h"

#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <new>
#include <string>
#include <utility>

namespace GameWIP::Input::Platform::Win32::Detail
{
    // ------------------------------------------------------------
    // Focus, keyboard, gamepad, and text event translation
    // ------------------------------------------------------------
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

    [[nodiscard]] static std::uint32_t keyboardUsage(unsigned int scanCode, bool extended, bool extendedPause, unsigned int virtualKey) noexcept
    {
        if(virtualKey == VK_PAUSE || extendedPause)
        {
            return 0x48;
        }

        if(extended)
        {
            switch(scanCode)
            {
            case 0x1C:
                return 0x58;
            case 0x1D:
                return 0xE4;
            case 0x35:
                return 0x54;
            case 0x37:
                return 0x46;
            case 0x38:
                return 0xE6;
            case 0x47:
                return 0x4A;
            case 0x48:
                return 0x52;
            case 0x49:
                return 0x4B;
            case 0x4B:
                return 0x50;
            case 0x4D:
                return 0x4F;
            case 0x4F:
                return 0x4D;
            case 0x50:
                return 0x51;
            case 0x51:
                return 0x4E;
            case 0x52:
                return 0x49;
            case 0x53:
                return 0x4C;
            case 0x5B:
                return 0xE3;
            case 0x5C:
                return 0xE7;
            case 0x5D:
                return 0x65;
            default:
                return 0;
            }
        }

        constexpr std::array<std::uint32_t, 0x59> scanToUsage{
            0,    0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B, 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18,
            0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0x2C, 0xE1, 0x31, 0x1D,
            0x1B, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE2, 0x55, 0xE6, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43,
            0x44, 0x45, 0x53, 0x47, 0x5F, 0x60, 0x61, 0x5C, 0x5D, 0x5E, 0x59, 0x5A, 0x5B, 0x62, 0x63, 0x54, 0x55, 0x56, 0x57, 0x58};

        if(scanCode < scanToUsage.size())
        {
            return scanToUsage[scanCode];
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

        if(GameWIP::Unicode::Utf8::validate(utf8).outcome != GameWIP::Unicode::Types::ValidationOutcome::Valid)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }
        const bool committed = kind == Types::EventKind::TextCommitted;
        if(committed && (state.pendingTextUtf8.size() > state.configuration.maximumPendingTextBytes ||
                         utf8.size() > state.configuration.maximumPendingTextBytes - state.pendingTextUtf8.size()))
        {
            state.pendingDiscontinuity = true;
            return;
        }

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
        if(!validScalar(scalar))
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }
        const auto encoded = GameWIP::Unicode::Utf8::encodeScalar(scalar);
        if(encoded.outcome != GameWIP::Unicode::Types::EncodeOutcome::Encoded)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }
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
        const LONG byteCount = ImmGetCompositionStringW(context, compositionFlag, nullptr, 0);
        if(byteCount < 0 || (byteCount % static_cast<LONG>(sizeof(wchar_t))) != 0)
        {
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
            return;
        }

        try
        {
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

        if(attachment.pendingHighSurrogate != 0)
        {
            attachment.pendingHighSurrogate = 0;
            ++state.health.totalInvalidReports;
            state.pendingDiscontinuity = true;
        }

        appendText(state, attachment, static_cast<char32_t>(codeUnit));
    }

} // namespace GameWIP::Input::Platform::Win32::Detail
