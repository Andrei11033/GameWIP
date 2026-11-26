/// @file stream.cpp
/// @brief Default Reader and Writer behavior.

#include "io/stream.h"

namespace GameWIP::IO
{
    namespace ResultTypes = GameWIP::Status::Types;

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

    ResultTypes::Result<void> Reader::close() noexcept
    {
        return ResultTypes::Result<void>::success();
    }

    ResultTypes::Result<std::uint64_t> Reader::position() const noexcept
    {
        return ResultTypes::Result<std::uint64_t>::failure(ResultTypes::Status(ResultTypes::Code::NotSeekable));
    }

    ResultTypes::Result<std::uint64_t> Reader::size() const noexcept
    {
        return ResultTypes::Result<std::uint64_t>::failure(ResultTypes::Status(ResultTypes::Code::NotSeekable));
    }

    ResultTypes::Result<void> Reader::seek([[maybe_unused]] std::int64_t offset, [[maybe_unused]] Types::SeekOrigin origin) noexcept
    {
        return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::NotSeekable));
    }

    bool Writer::isOpen() const noexcept
    {
        return true;
    }

    bool Writer::canSeek() const noexcept
    {
        return false;
    }

    ResultTypes::Result<void> Writer::flush(Types::FlushMode mode) noexcept
    {
        if(!isValidFlushMode(mode))
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        return ResultTypes::Result<void>::success();
    }

    ResultTypes::Result<void> Writer::close() noexcept
    {
        return ResultTypes::Result<void>::success();
    }

    ResultTypes::Result<std::uint64_t> Writer::position() const noexcept
    {
        return ResultTypes::Result<std::uint64_t>::failure(ResultTypes::Status(ResultTypes::Code::NotSeekable));
    }

    ResultTypes::Result<void> Writer::seek([[maybe_unused]] std::int64_t offset, [[maybe_unused]] Types::SeekOrigin origin) noexcept
    {
        return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::NotSeekable));
    }

} // namespace GameWIP::IO
