/// @file memory_writer.cpp
/// @brief MemoryWriter operations.

#include "io/memory.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace GameWIP::IO
{
    namespace ResultTypes = GameWIP::Status::Types;

    namespace
    {
        /// @brief Appends a range already owned by a byte vector without allocating a temporary copy.
        /// @param destination Destination vector that also owns the source bytes.
        /// @param sourceOffset Byte offset of the source range in destination.
        /// @param sourceSize Number of source bytes to append.
        /// @return Success, SizeLimitExceeded for a representational limit, or OutOfMemory for allocation failure.
        [[nodiscard]] ResultTypes::Result<void> appendAliasedBytes(std::vector<std::byte> &destination, std::size_t sourceOffset, std::size_t sourceSize) noexcept
        {
            const auto oldSize = destination.size();

            if(sourceOffset > oldSize || sourceSize > oldSize - sourceOffset)
            {
                return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
            }

            if(sourceSize > destination.max_size() - oldSize)
            {
                return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
            }

            // sourceOffset remains valid even if resize reallocates and invalidates the original span.
            try
            {
                destination.resize(oldSize + sourceSize);
            }
            catch(const std::bad_alloc &)
            {
                return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::OutOfMemory));
            }
            catch(const std::length_error &)
            {
                return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
            }
            catch(...)
            {
                return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::Unknown));
            }

            const std::span storage(destination);
            std::ranges::copy(storage.subspan(sourceOffset, sourceSize), storage.subspan(oldSize, sourceSize).begin());
            return ResultTypes::Result<void>::success();
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Memory writer
    /// ------------------------------------------------------------

    bool MemoryWriter::isOpen() const noexcept
    {
        return open_;
    }

    bool MemoryWriter::canSeek() const noexcept
    {
        return false;
    }

    ResultTypes::Result<Types::WriteProgress> MemoryWriter::write(std::span<const std::byte> bytes) noexcept
    {
        if(!open_)
        {
            return ResultTypes::Result<Types::WriteProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::NotOpen), Types::WriteProgress{.bytesWritten = 0});
        }

        if(bytes.empty())
        {
            return ResultTypes::Result<Types::WriteProgress>::success({.bytesWritten = 0});
        }

        const auto oldSize = bytes_.size();
        if(bytes.size() > bytes_.max_size() - oldSize)
        {
            return ResultTypes::Result<Types::WriteProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded), Types::WriteProgress{.bytesWritten = 0});
        }

        try
        {
            // Detect self-aliasing before appending can reallocate the vector. The source offset,
            // rather than the input pointer, remains usable after storage moves.
            const std::byte *const inputBegin = bytes.data();
            const std::byte *const ownBegin = bytes_.data();
            const std::byte *const ownEnd = ownBegin == nullptr ? nullptr : std::to_address(std::span{bytes_}.end());
            const std::less<const std::byte *> pointerLess;

            if(ownBegin != nullptr && !pointerLess(inputBegin, ownBegin) && pointerLess(inputBegin, ownEnd))
            {
                const auto sourceOffset = static_cast<std::size_t>(inputBegin - ownBegin);
                auto appendStatus = appendAliasedBytes(bytes_, sourceOffset, bytes.size());
                if(!appendStatus.ok())
                {
                    return ResultTypes::Result<Types::WriteProgress>::failure(
                        std::move(appendStatus).status(), Types::WriteProgress{.bytesWritten = 0});
                }

                return ResultTypes::Result<Types::WriteProgress>::success({.bytesWritten = bytes.size()});
            }

            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());

            return ResultTypes::Result<Types::WriteProgress>::success({.bytesWritten = bytes.size()});
        }
        catch(const std::bad_alloc &)
        {
            return ResultTypes::Result<Types::WriteProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::OutOfMemory), Types::WriteProgress{.bytesWritten = 0});
        }
        catch(const std::length_error &)
        {
            return ResultTypes::Result<Types::WriteProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded), Types::WriteProgress{.bytesWritten = 0});
        }
        catch(...)
        {
            return ResultTypes::Result<Types::WriteProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::Unknown), Types::WriteProgress{.bytesWritten = 0});
        }
    }

    ResultTypes::Result<void> MemoryWriter::flush(Types::FlushMode mode) noexcept
    {
        if(!isValidFlushMode(mode))
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        if(!open_)
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::NotOpen));
        }

        return ResultTypes::Result<void>::success();
    }

    ResultTypes::Result<void> MemoryWriter::close() noexcept
    {
        open_ = false;
        return ResultTypes::Result<void>::success();
    }

    ResultTypes::Result<std::uint64_t> MemoryWriter::position() const noexcept
    {
        if(!open_)
        {
            return ResultTypes::Result<std::uint64_t>::failure(ResultTypes::Status(ResultTypes::Code::NotOpen));
        }

        return ResultTypes::Result<std::uint64_t>::success(static_cast<std::uint64_t>(bytes_.size()));
    }

    ResultTypes::Result<void> MemoryWriter::reserve(std::size_t capacity) noexcept
    {
        if(capacity > bytes_.max_size())
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
        }

        try
        {
            bytes_.reserve(capacity);
            return ResultTypes::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::OutOfMemory));
        }
        catch(const std::length_error &)
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
        }
        catch(...)
        {
            return ResultTypes::Result<void>::failure(ResultTypes::Status(ResultTypes::Code::Unknown));
        }
    }

    std::span<const std::byte> MemoryWriter::bytes() const noexcept
    {
        return bytes_;
    }

    ResultTypes::Result<std::string> MemoryWriter::copyText() const noexcept
    {
        const auto view = bytes();

        if(view.empty())
        {
            return ResultTypes::Result<std::string>::success({});
        }

        if(view.size() > std::string{}.max_size())
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
        }

        const std::string_view textView(reinterpret_cast<const char *>(view.data()), view.size());

        if(Unicode::Utf8::validate(textView).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::EncodingFailed));
        }

        try
        {
            std::string text(textView.data(), textView.size());
            return ResultTypes::Result<std::string>::success(std::move(text));
        }
        catch(const std::bad_alloc &)
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::OutOfMemory));
        }
        catch(const std::length_error &)
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::SizeLimitExceeded));
        }
        catch(...)
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::Unknown));
        }
    }

    std::vector<std::byte> MemoryWriter::takeBytes() noexcept
    {
        return std::exchange(bytes_, {});
    }

    std::size_t MemoryWriter::size() const noexcept
    {
        return bytes_.size();
    }

    std::size_t MemoryWriter::capacity() const noexcept
    {
        return bytes_.capacity();
    }

    bool MemoryWriter::empty() const noexcept
    {
        return bytes_.empty();
    }

    void MemoryWriter::clear() noexcept
    {
        bytes_.clear();
    }
} // namespace GameWIP::IO
