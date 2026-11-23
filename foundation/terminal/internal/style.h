/// @file style.h
/// @brief Private Terminal style planning and escape-sequence helpers.

#pragma once

#include "terminal/output.h"

#include <array>
#include <cstddef>

namespace GameWIP::Terminal::Internal
{
    struct StylePlan
    {
        IO::Types::Status status = IO::successStatus();
        bool emitStyle = false;
        Types::Style::Request style{};
    };

    struct StyleSequence
    {
        std::array<char, 96> bytes{};
        std::size_t size = 0;
    };

    [[nodiscard]] bool isDefaultStyle(const Types::Style::Request &style) noexcept;
    [[nodiscard]] bool anyStyleSupported(const Types::Style::Capabilities &capabilities) noexcept;
    [[nodiscard]] StylePlan stylePlanForCapabilities(
        Types::Style::Mode mode,
        const Types::Style::Request &style,
        const Types::Style::Capabilities &capabilities);
    [[nodiscard]] StylePlan stylePlan(Types::Output::Stream stream, Types::Style::Mode mode, const Types::Style::Request &style);
    [[nodiscard]] StyleSequence makeStyleSequence(const Types::Style::Request &style) noexcept;
} // namespace GameWIP::Terminal::Internal
