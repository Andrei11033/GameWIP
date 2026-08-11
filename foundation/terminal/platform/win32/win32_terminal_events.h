/// @file win32_terminal_events.h
/// @brief Internal Win32 console-record to portable Terminal event decoding.

#pragma once

#include "terminal/terminal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace GameWIP::Terminal::Detail::Platform::Win32Events
{
    /// @brief Result classification for one KEY_EVENT_RECORD decode attempt.
    enum class KeyDecodeDisposition : std::uint8_t
    {
        Produced,
        Pending,
        Ignored,
        Failed
    };

    /// @brief Persistent allocation-free logical decoder state for one observed Win32 stdin endpoint.
    struct DecoderState
    {
        std::bitset<std::size_t{256} * std::size_t{5}> keyDown{};
        char16_t pendingHighSurrogate = u'\0';
        KEY_EVENT_RECORD pendingHighSurrogateRecord{};
        std::optional<Terminal::Types::Event> pendingEvent;

        void clear() noexcept;
    };

    /// @brief Result of decoding one native Win32 key record.
    struct KeyDecodeResult
    {
        IO::Types::Status status = IO::successStatus();
        KeyDecodeDisposition disposition = KeyDecodeDisposition::Ignored;
        std::optional<Terminal::Types::Event> event;
    };

    /// @brief Returns and clears a logical event retained from a combined native repeat record.
    [[nodiscard]] std::optional<Terminal::Types::Event> takePendingEvent(DecoderState &state) noexcept;

    /// @brief Converts one Win32 KEY_EVENT_RECORD into a portable logical key event.
    /// @details UTF-16 surrogate pairs, key-down tracking, repeat normalization, modifiers, AltGr text,
    /// named/function/media keys, and location are normalized without implementation-owned allocation.
    [[nodiscard]] KeyDecodeResult decodeKeyRecord(const KEY_EVENT_RECORD &record, DecoderState &state) noexcept;
} // namespace GameWIP::Terminal::Detail::Platform::Win32Events
