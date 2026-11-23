/// @file session_output.cpp
/// @brief Session output and capability methods.

#include "terminal/internal/session_state.h"
#include "terminal/internal/status.h"
#include "terminal/session.h"
#include "terminal/terminal.h"

#include <cstddef>
#include <format>
#include <span>
#include <string_view>

namespace GameWIP::Terminal
{
    using Internal::notOpenStatus;

    /// ------------------------------------------------------------
    /// Output capabilities
    /// ------------------------------------------------------------

    Types::Output::CapabilitiesResult Session::getOutputCapabilities() const noexcept
    {
        if(!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return Terminal::getOutputCapabilities(state_->options.output);
        }
        catch(...)
        {
            return {.status = Internal::exceptionStatus(), .capabilities = {}};
        }
    }

    /// ------------------------------------------------------------
    /// Output
    /// ------------------------------------------------------------

    Types::Output::CapabilitiesResult Session::prepareOutput() noexcept
    {
        if(!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }

            return Terminal::prepareOutput(state_->options.output);
        }
        catch(...)
        {
            return {.status = Internal::exceptionStatus(), .capabilities = {}};
        }
    }

    Types::SizeResult Session::getTerminalSize() const noexcept
    {
        if(!state_)
        {
            return {.status = notOpenStatus(), .size = {}};
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .size = {}};
            }
            return Terminal::getTerminalSize(state_->options.output);
        }
        catch(...)
        {
            return {.status = Internal::exceptionStatus(), .size = {}};
        }
    }

    IO::Types::Status Session::writeText(std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        if(!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeText(state_->options.output, utf8Text, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::writeLine(std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        if(!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeLine(state_->options.output, utf8Text, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::WriteResult Session::writeBytes(std::span<const std::byte> bytes, const Types::Output::ByteOptions &options) noexcept
    {
        if(!state_)
        {
            return {.status = notOpenStatus(), .bytesWritten = 0};
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .bytesWritten = 0};
            }
            return Terminal::writeBytes(state_->options.output, bytes, options);
        }
        catch(...)
        {
            return {.status = Internal::exceptionStatus(), .bytesWritten = 0};
        }
    }

    IO::Types::Status Session::writeSegments(std::span<const Types::Output::Segment> segments, const Types::Output::SegmentOptions &options) noexcept
    {
        if(!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeSegments(state_->options.output, segments, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::vprint(const Types::Output::TextOptions &options, std::string_view format, std::format_args arguments) noexcept
    {
        if(!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Internal::vprint(state_->options.output, options, format, arguments);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::vprintln(const Types::Output::LineOptions &options, std::string_view format, std::format_args arguments) noexcept
    {
        if(!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Internal::vprintln(state_->options.output, options, format, arguments);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::flush(IO::Types::FlushMode mode) noexcept
    {
        if(!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::flush(state_->options.output, mode);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

} // namespace GameWIP::Terminal
