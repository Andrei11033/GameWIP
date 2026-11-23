/// @file stream.cpp
/// @brief Default Reader and Writer behavior.

#include "io/stream.h"

namespace GameWIP::IO
{
    /// ------------------------------------------------------------
    /// Default stream behavior
    /// ------------------------------------------------------------

    bool Reader::isOpen() const noexcept
    {
        return true;
    }

    bool Reader::canSeek() const noexcept
    {
        return false;
    }

    Types::Status Reader::close() noexcept
    {
        return successStatus();
    }

    Types::PositionResult Reader::position() const noexcept
    {
        return {.status = makeStatus(Types::ErrorCode::NotSeekable), .position = 0};
    }

    Types::SizeResult Reader::size() const noexcept
    {
        return {.status = makeStatus(Types::ErrorCode::NotSeekable), .sizeBytes = 0};
    }

    Types::Status Reader::seek([[maybe_unused]] std::int64_t offset, [[maybe_unused]] Types::SeekOrigin origin) noexcept
    {
        return makeStatus(Types::ErrorCode::NotSeekable);
    }

    bool Writer::isOpen() const noexcept
    {
        return true;
    }

    bool Writer::canSeek() const noexcept
    {
        return false;
    }

    Types::Status Writer::flush(Types::FlushMode mode) noexcept
    {
        if(!isValidFlushMode(mode))
        {
            return makeStatus(Types::ErrorCode::InvalidArgument);
        }

        return successStatus();
    }

    Types::Status Writer::close() noexcept
    {
        return successStatus();
    }

    Types::PositionResult Writer::position() const noexcept
    {
        return {.status = makeStatus(Types::ErrorCode::NotSeekable), .position = 0};
    }

    Types::Status Writer::seek([[maybe_unused]] std::int64_t offset, [[maybe_unused]] Types::SeekOrigin origin) noexcept
    {
        return makeStatus(Types::ErrorCode::NotSeekable);
    }

} // namespace GameWIP::IO
