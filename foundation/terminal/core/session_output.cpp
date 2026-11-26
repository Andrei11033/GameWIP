/// @file session_output.cpp
/// @brief Session output and capability methods.

#include "terminal/internal/session_state.h"
#include "terminal/internal/results.h"
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
            return Internal::failure<Types::Output::Capabilities>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::failure<Types::Output::Capabilities>(notOpenStatus());
            }
            return Terminal::getOutputCapabilities(state_->options.output);
        }
        catch(...)
        {
            return Internal::failure<Types::Output::Capabilities>(Internal::exceptionStatus());
        }
    }

    /// ------------------------------------------------------------
    /// Output
    /// ------------------------------------------------------------

    Types::Output::CapabilitiesResult Session::prepareOutput() noexcept
    {
        if(!state_)
        {
            return Internal::failure<Types::Output::Capabilities>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::failure<Types::Output::Capabilities>(notOpenStatus());
            }

            return Terminal::prepareOutput(state_->options.output);
        }
        catch(...)
        {
            return Internal::failure<Types::Output::Capabilities>(Internal::exceptionStatus());
        }
    }

    Types::SizeResult Session::getTerminalSize() const noexcept
    {
        if(!state_)
        {
            return Internal::failure<Types::Size>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::failure<Types::Size>(notOpenStatus());
            }
            return Terminal::getTerminalSize(state_->options.output);
        }
        catch(...)
        {
            return Internal::failure<Types::Size>(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::writeText(std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        if(!state_)
        {
            return Internal::result(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(notOpenStatus());
            }
            return Terminal::writeText(state_->options.output, utf8Text, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::writeLine(std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        if(!state_)
        {
            return Internal::result(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(notOpenStatus());
            }
            return Terminal::writeLine(state_->options.output, utf8Text, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress> Session::writeBytes(
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options) noexcept
    {
        if(!state_)
        {
            return Internal::failure<IO::Types::WriteProgress>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::failure<IO::Types::WriteProgress>(notOpenStatus());
            }
            return Terminal::writeBytes(state_->options.output, bytes, options);
        }
        catch(...)
        {
            return Internal::failure<IO::Types::WriteProgress>(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::writeSegments(
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options) noexcept
    {
        if(!state_)
        {
            return Internal::result(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(notOpenStatus());
            }
            return Terminal::writeSegments(state_->options.output, segments, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::vprint(
        const Types::Output::TextOptions &options,
        std::string_view format,
        std::format_args arguments) noexcept
    {
        if(!state_)
        {
            return Internal::result(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(notOpenStatus());
            }
            return Internal::result(Internal::vprint(state_->options.output, options, format, arguments));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::vprintln(
        const Types::Output::LineOptions &options,
        std::string_view format,
        std::format_args arguments) noexcept
    {
        if(!state_)
        {
            return Internal::result(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(notOpenStatus());
            }
            return Internal::result(Internal::vprintln(state_->options.output, options, format, arguments));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::flush(IO::Types::FlushMode mode) noexcept
    {
        if(!state_)
        {
            return Internal::result(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(notOpenStatus());
            }
            return Terminal::flush(state_->options.output, mode);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

} // namespace GameWIP::Terminal
