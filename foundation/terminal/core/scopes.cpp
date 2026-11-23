/// @file scopes.cpp
/// @brief Output segment helpers and scoped terminal state.

#include "terminal/output.h"

#include "terminal/internal/scopes.h"

#include <span>
#include <string_view>
#include <utility>

namespace GameWIP::Terminal
{
    namespace
    {
        /// @brief Copies a stored status without letting optional diagnostic allocation escape a noexcept boundary.
        [[nodiscard]] IO::Types::Status copyStatus(const IO::Types::Status &status) noexcept
        {
            if(status.message.empty())
            {
                return IO::makeStatus(status.code, status.nativeCode);
            }

            try
            {
                return IO::makeStatus(status.code, status.nativeCode, status.message);
            }
            catch(...)
            {
                return IO::makeStatus(status.code, status.nativeCode);
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Output segment construction
    /// ------------------------------------------------------------

    Types::Output::Segment::Segment(
        Types::Output::SegmentKind kind,
        std::string_view text,
        std::span<const std::byte> bytes,
        const Types::Style::Request &style) noexcept
        : kind_(kind)
        , text_(text)
        , bytes_(bytes)
        , style_(style)
    {
    }

    Types::Output::Segment textSegment(std::string_view text) noexcept
    {
        return Types::Output::Segment(Types::Output::SegmentKind::Text, text, {}, {});
    }

    Types::Output::Segment styledTextSegment(std::string_view text, const Types::Style::Request &style) noexcept
    {
        return Types::Output::Segment(Types::Output::SegmentKind::StyledText, text, {}, style);
    }

    Types::Output::Segment byteSegment(std::span<const std::byte> bytes) noexcept
    {
        return Types::Output::Segment(Types::Output::SegmentKind::Bytes, {}, bytes, {});
    }

    /// ------------------------------------------------------------
    /// Scoped terminal state
    /// ------------------------------------------------------------

    AlternateScreenScope::AlternateScreenScope() noexcept = default;

    AlternateScreenScope::AlternateScreenScope(AlternateScreenScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
        , restorationEmitted_(other.restorationEmitted_)
        , restoreOnDestruction_(other.restoreOnDestruction_)
    {
        other.active_ = false;
        other.restorationEmitted_ = false;
        other.restoreOnDestruction_ = true;
    }

    AlternateScreenScope &AlternateScreenScope::operator=(AlternateScreenScope &&other) noexcept
    {
        if(this != &other)
        {
            if(active_)
            {
                // Preserve the restoration obligation if leaving the current scope fails.
                static_cast<void>(leave());

                if(active_)
                {
                    return *this;
                }
            }

            stream_ = other.stream_;
            options_ = other.options_;
            status_ = std::move(other.status_);
            active_ = other.active_;
            restorationEmitted_ = other.restorationEmitted_;
            restoreOnDestruction_ = other.restoreOnDestruction_;
            other.active_ = false;
            other.restorationEmitted_ = false;
            other.restoreOnDestruction_ = true;
        }

        return *this;
    }

    AlternateScreenScope::~AlternateScreenScope() noexcept
    {
        if(restoreOnDestruction_)
        {
            static_cast<void>(leave());
        }

        if(active_)
        {
            Internal::abandonAlternateScreenScope(stream_);
            active_ = false;
            restorationEmitted_ = false;
        }
    }

    bool AlternateScreenScope::active() const noexcept
    {
        return active_;
    }

    const IO::Types::Status &AlternateScreenScope::status() const noexcept
    {
        return status_;
    }

    IO::Types::Status AlternateScreenScope::leave() noexcept
    {
        if(active_)
        {
            if(restorationEmitted_)
            {
                status_ = Terminal::flush(stream_, options_.flushMode);

                if(status_.ok())
                {
                    active_ = false;
                    restorationEmitted_ = false;
                }
            }
            else
            {
                bool restored = false;
                status_ = Internal::leaveAlternateScreenScope(stream_, options_, restored);

                if(restored)
                {
                    if(status_.ok())
                    {
                        active_ = false;
                    }
                    else
                    {
                        restorationEmitted_ = true;
                    }
                }
            }
        }

        return copyStatus(status_);
    }

    CursorHiddenScope::CursorHiddenScope() noexcept = default;

    CursorHiddenScope::CursorHiddenScope(CursorHiddenScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
        , restorationEmitted_(other.restorationEmitted_)
        , restoreOnDestruction_(other.restoreOnDestruction_)
    {
        other.active_ = false;
        other.restorationEmitted_ = false;
        other.restoreOnDestruction_ = true;
    }

    CursorHiddenScope &CursorHiddenScope::operator=(CursorHiddenScope &&other) noexcept
    {
        if(this != &other)
        {
            if(active_)
            {
                // Preserve the restoration obligation if restoring visibility fails.
                static_cast<void>(restore());

                if(active_)
                {
                    return *this;
                }
            }

            stream_ = other.stream_;
            options_ = other.options_;
            status_ = std::move(other.status_);
            active_ = other.active_;
            restorationEmitted_ = other.restorationEmitted_;
            restoreOnDestruction_ = other.restoreOnDestruction_;
            other.active_ = false;
            other.restorationEmitted_ = false;
            other.restoreOnDestruction_ = true;
        }

        return *this;
    }

    CursorHiddenScope::~CursorHiddenScope() noexcept
    {
        if(restoreOnDestruction_)
        {
            static_cast<void>(restore());
        }

        if(active_)
        {
            Internal::abandonCursorHiddenScope(stream_);
            active_ = false;
            restorationEmitted_ = false;
        }
    }

    bool CursorHiddenScope::active() const noexcept
    {
        return active_;
    }

    const IO::Types::Status &CursorHiddenScope::status() const noexcept
    {
        return status_;
    }

    IO::Types::Status CursorHiddenScope::restore() noexcept
    {
        if(active_)
        {
            if(restorationEmitted_)
            {
                status_ = Terminal::flush(stream_, options_.flushMode);

                if(status_.ok())
                {
                    active_ = false;
                    restorationEmitted_ = false;
                }
            }
            else
            {
                bool restored = false;
                status_ = Internal::restoreCursorHiddenScope(stream_, options_, restored);

                if(restored)
                {
                    if(status_.ok())
                    {
                        active_ = false;
                    }
                    else
                    {
                        restorationEmitted_ = true;
                    }
                }
            }
        }

        return copyStatus(status_);
    }
} // namespace GameWIP::Terminal
