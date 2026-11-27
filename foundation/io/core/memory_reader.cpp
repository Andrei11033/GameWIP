/// @file memory_reader.cpp
/// @brief MemoryReader operations.

#include "io/memory.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>

namespace GameWIP::IO
{
    namespace ResultTypes = GameWIP::Status::Types;

    /// ------------------------------------------------------------
    /// Memory reader
    /// ------------------------------------------------------------

    MemoryReader::MemoryReader(std::span<const std::byte> bytes) noexcept
        : bytes_(bytes)
    {
    }

    MemoryReader::MemoryReader(std::string_view bytes) noexcept
        : MemoryReader(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())))
    {
    }

    bool MemoryReader::isOpen() const noexcept
    {
        return open_;
    }

    bool MemoryReader::canSeek() const noexcept
    {
        return isOpen();
    }

    ResultTypes::Result<Types::ReadProgress> MemoryReader::read(std::span<std::byte> destination) noexcept
    {
        if(!open_)
        {
            return ResultTypes::Result<Types::ReadProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::NotOpen),
                Types::ReadProgress{.bytesRead = 0, .endOfStream = false});
        }

        if(position_ > bytes_.size())
        {
            return ResultTypes::Result<Types::ReadProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::InvalidArgument),
                Types::ReadProgress{.bytesRead = 0, .endOfStream = false});
        }

        if(destination.empty())
        {
            return ResultTypes::Result<Types::ReadProgress>::success({.bytesRead = 0, .endOfStream = position_ >= bytes_.size()});
        }

        if(position_ >= bytes_.size())
        {
            return ResultTypes::Result<Types::ReadProgress>::success({.bytesRead = 0, .endOfStream = true});
        }

        const auto available = bytes_.size() - position_;
        const auto count = std::min(destination.size(), available);

        const std::span source = bytes_.subspan(position_, count);
        const std::span target = destination.first(count);
        const std::less<const std::byte *> pointerLess;

        if(pointerLess(source.data(), target.data()) && pointerLess(target.data(), std::to_address(source.end())))
        {
            std::ranges::copy_backward(source, target.end());
        }
        else
        {
            std::ranges::copy(source, target.begin());
        }

        position_ += count;

        return ResultTypes::Result<Types::ReadProgress>::success({.bytesRead = count, .endOfStream = position_ == bytes_.size()});
    }

    ResultTypes::Result<void> MemoryReader::close() noexcept
    {
        open_ = false;
        return ResultTypes::Result<void>::success();
    }

    ResultTypes::Result<std::uint64_t> MemoryReader::position() const noexcept
    {
        if(!open_)
        {
            return ResultTypes::Result<std::uint64_t>::failure(ResultTypes::Status(ResultTypes::Code::NotOpen));
        }

        return ResultTypes::Result<std::uint64_t>::success(static_cast<std::uint64_t>(position_));
    }

    ResultTypes::Result<std::uint64_t> MemoryReader::size() const noexcept
    {
        if(!open_)
        {
            return ResultTypes::Result<std::uint64_t>::failure(ResultTypes::Status(ResultTypes::Code::NotOpen));
        }

        return ResultTypes::Result<std::uint64_t>::success(static_cast<std::uint64_t>(bytes_.size()));
    }

    ResultTypes::Result<void> MemoryReader::seek(std::int64_t offset, Types::SeekOrigin origin) noexcept
    {
        if(!open_)
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::NotOpen));
        }

        if(bytes_.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
        }

        const auto size = static_cast<std::int64_t>(bytes_.size());

        if(position_ > static_cast<std::size_t>(size))
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        std::int64_t base = 0;

        switch(origin)
        {
        case Types::SeekOrigin::Begin:
            base = 0;
            break;

        case Types::SeekOrigin::Current:
            base = static_cast<std::int64_t>(position_);
            break;

        case Types::SeekOrigin::End:
            base = size;
            break;

        default:
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        if(offset < -base || offset > size - base)
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::SeekFailed));
        }

        const auto target = base + offset;
        position_ = static_cast<std::size_t>(target);

        return ResultTypes::Result<void>::success();
    }

} // namespace GameWIP::IO
