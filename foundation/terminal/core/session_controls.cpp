/// @file session_controls.cpp
/// @brief Session cursor, style, screen, and title controls.

#include "terminal/internal/session_state.h"
#include "terminal/internal/results.h"
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

    GameWIP::Status::Types::Result<void> Session::resetStyle(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::resetStyle(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    /// ------------------------------------------------------------
    /// Cursor controls
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> Session::moveCursor(
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::moveCursor(state_->options.output, direction, amount, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::setCursorPosition(
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::setCursorPosition(state_->options.output, position, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    Types::Cursor::PositionResult Session::getCursorPosition(const Types::Cursor::QueryOptions &options) noexcept
    {
        if(!state_)
        {
            return Internal::failure<Types::Cursor::Position>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::failure<Types::Cursor::Position>(notOpenStatus());
            }
            return Terminal::getCursorPosition(state_->options.output, state_->options.input, options);
        }
        catch(...)
        {
            return Internal::failure<Types::Cursor::Position>(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::saveCursorPosition(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::saveCursorPosition(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::restoreCursorPosition(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::restoreCursorPosition(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::setCursorVisible(bool visible, const Types::Output::ControlOptions &options) noexcept
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

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return Internal::result(invalidArgumentStatus());
            }
            std::lock_guard persistentLock(state_->persistentOutputMutex);

            if(!visible)
            {
                if(state_->cursorHiddenScope.active())
                {
                    return GameWIP::Status::Types::Result<void>::success();
                }
                CursorHiddenScope scope = scopedCursorHidden(state_->options.output, options);

                if(scope.active())
                {
                    state_->cursorHiddenScope = std::move(scope);
                    state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::CursorHidden;
                    return Internal::result(state_->cursorHiddenScope.status());
                }
                return Internal::result(scope.status());
            }

            if(!state_->cursorHiddenScope.active())
            {
                return Terminal::setCursorVisible(state_->options.output, true, options);
            }
            state_->cursorHiddenScope.options_ = options;
            GameWIP::Status::Types::Result<void> status = state_->cursorHiddenScope.restore();

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

            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    /// ------------------------------------------------------------
    /// Screen controls
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> Session::clear(
        Types::Output::ClearTarget target,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::clear(state_->options.output, target, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::scroll(
        Types::Output::ScrollDirection direction,
        std::uint32_t lines,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::scroll(state_->options.output, direction, lines, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::enterAlternateScreen(const Types::Output::ControlOptions &options) noexcept
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

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return Internal::result(invalidArgumentStatus());
            }
            std::lock_guard persistentLock(state_->persistentOutputMutex);

            if(state_->alternateScreenScope.active())
            {
                return GameWIP::Status::Types::Result<void>::success();
            }
            AlternateScreenScope scope = scopedAlternateScreen(state_->options.output, options);

            if(scope.active())
            {
                state_->alternateScreenScope = std::move(scope);
                state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::AlternateScreen;
                return Internal::result(state_->alternateScreenScope.status());
            }
            return Internal::result(scope.status());
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::leaveAlternateScreen(const Types::Output::ControlOptions &options) noexcept
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

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return Internal::result(invalidArgumentStatus());
            }
            std::lock_guard persistentLock(state_->persistentOutputMutex);

            if(!state_->alternateScreenScope.active())
            {
                return Terminal::leaveAlternateScreen(state_->options.output, options);
            }
            state_->alternateScreenScope.options_ = options;
            GameWIP::Status::Types::Result<void> status = state_->alternateScreenScope.leave();

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

            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    /// ------------------------------------------------------------
    /// Title and bell
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> Session::setTitle(
        std::string_view utf8Title,
        const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::setTitle(state_->options.output, utf8Title, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> Session::ringBell(const Types::Output::ControlOptions &options) noexcept
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
            return Terminal::ringBell(state_->options.output, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

} // namespace GameWIP::Terminal
