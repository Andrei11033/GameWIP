/// @file session_controls.cpp
/// @brief Session cursor, style, screen, and title controls.

#include "terminal/internal/session_state.h"
#include "terminal/internal/status.h"
#include "terminal/session.h"
#include "terminal/terminal.h"

namespace GameWIP::Terminal
{
    using Internal::invalidArgumentStatus;
    using Internal::notOpenStatus;

    /// ------------------------------------------------------------
    /// Terminal controls
    /// ------------------------------------------------------------

    IO::Types::Status Session::resetStyle(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::resetStyle(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    /// ------------------------------------------------------------
    /// Cursor controls
    /// ------------------------------------------------------------

    IO::Types::Status Session::moveCursor(
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::moveCursor(state_->options.output, direction, amount, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::setCursorPosition(Types::Cursor::Position position, const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::setCursorPosition(state_->options.output, position, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    Types::Cursor::PositionResult Session::getCursorPosition(const Types::Cursor::QueryOptions &options) noexcept
    {
        if(!state_)
        {
            return {.status = notOpenStatus(), .position = {}};
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .position = {}};
            }
            return Terminal::getCursorPosition(state_->options.output, state_->options.input, options);
        }
        catch(...)
        {
            return {.status = Internal::exceptionStatus(), .position = {}};
        }
    }

    IO::Types::Status Session::saveCursorPosition(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::saveCursorPosition(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::restoreCursorPosition(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::restoreCursorPosition(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::setCursorVisible(bool visible, const Types::Output::ControlOptions &options) noexcept
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

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus();
            }
            std::lock_guard persistentLock(state_->persistentOutputMutex);

            if(!visible)
            {
                if(state_->cursorHiddenScope.active())
                {
                    return IO::successStatus();
                }
                CursorHiddenScope scope = scopedCursorHidden(state_->options.output, options);

                if(scope.active())
                {
                    state_->cursorHiddenScope = std::move(scope);
                    state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::CursorHidden;
                    return state_->cursorHiddenScope.status();
                }
                return scope.status();
            }

            if(!state_->cursorHiddenScope.active())
            {
                return Terminal::setCursorVisible(state_->options.output, true, options);
            }
            state_->cursorHiddenScope.options_ = options;
            IO::Types::Status status = state_->cursorHiddenScope.restore();

            if(!status.ok())
            {
                return status;
            }

            for(std::size_t index = 0; index < state_->outputRestoreCount; ++index)
            {
                if(state_->outputRestoreOrder[index] == State::PersistentOutputState::CursorHidden)
                {
                    for(std::size_t next = index + 1; next < state_->outputRestoreCount; ++next)
                    {
                        state_->outputRestoreOrder[next - 1] = state_->outputRestoreOrder[next];
                    }
                    --state_->outputRestoreCount;
                    break;
                }
            }

            return IO::successStatus();
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    /// ------------------------------------------------------------
    /// Screen controls
    /// ------------------------------------------------------------

    IO::Types::Status Session::clear(Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::clear(state_->options.output, target, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::scroll(
        Types::Output::ScrollDirection direction,
        std::uint32_t lines,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::scroll(state_->options.output, direction, lines, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::enterAlternateScreen(const Types::Output::ControlOptions &options) noexcept
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

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus();
            }
            std::lock_guard persistentLock(state_->persistentOutputMutex);

            if(state_->alternateScreenScope.active())
            {
                return IO::successStatus();
            }
            AlternateScreenScope scope = scopedAlternateScreen(state_->options.output, options);

            if(scope.active())
            {
                state_->alternateScreenScope = std::move(scope);
                state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::AlternateScreen;
                return state_->alternateScreenScope.status();
            }
            return scope.status();
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::leaveAlternateScreen(const Types::Output::ControlOptions &options) noexcept
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

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus();
            }
            std::lock_guard persistentLock(state_->persistentOutputMutex);

            if(!state_->alternateScreenScope.active())
            {
                return Terminal::leaveAlternateScreen(state_->options.output, options);
            }
            state_->alternateScreenScope.options_ = options;
            IO::Types::Status status = state_->alternateScreenScope.leave();

            if(!status.ok())
            {
                return status;
            }

            for(std::size_t index = 0; index < state_->outputRestoreCount; ++index)
            {
                if(state_->outputRestoreOrder[index] == State::PersistentOutputState::AlternateScreen)
                {
                    for(std::size_t next = index + 1; next < state_->outputRestoreCount; ++next)
                    {
                        state_->outputRestoreOrder[next - 1] = state_->outputRestoreOrder[next];
                    }
                    --state_->outputRestoreCount;
                    break;
                }
            }

            return IO::successStatus();
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    /// ------------------------------------------------------------
    /// Title and bell
    /// ------------------------------------------------------------

    IO::Types::Status Session::setTitle(std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::setTitle(state_->options.output, utf8Title, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    IO::Types::Status Session::ringBell(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::ringBell(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

} // namespace GameWIP::Terminal
