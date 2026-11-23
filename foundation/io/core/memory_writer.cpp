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
    namespace
    {
        /// @brief Appends a range already owned by a byte vector without allocating a temporary copy.
        /// @param destination Destination vector that also owns the source bytes.
        /// @param sourceOffset Byte offset of the source range in destination.
        /// @param sourceSize Number of source bytes to append.
        /// @return Success, SizeLimitExceeded for a representational limit, or OutOfMemory for allocation failure.
        [[nodiscard]] Types::Status appendAliasedBytes(std::vector<std::byte> &destination, std::size_t sourceOffset, std::size_t sourceSize) noexcept
        {
            const auto oldSize = destination.size();

            if(sourceOffset > oldSize || sourceSize > oldSize - sourceOffset)
            {
                return makeStatus(Types::ErrorCode::InvalidArgument);
            }

            if(sourceSize > destination.max_size() - oldSize)
            {
                return makeStatus(Types::ErrorCode::SizeLimitExceeded);
            }

            // sourceOffset remains valid even if resize reallocates and invalidates the original span.
            try
            {
                destination.resize(oldSize + sourceSize);
            }
            catch(const std::bad_alloc &)
            {
                return makeStatus(Types::ErrorCode::OutOfMemory);
            }
            catch(const std::length_error &)
            {
                return makeStatus(Types::ErrorCode::SizeLimitExceeded);
            }
            catch(...)
            {
                return makeStatus(Types::ErrorCode::Unknown);
            }

            const std::span storage(destination);
            std::ranges::copy(storage.subspan(sourceOffset, sourceSize), storage.subspan(oldSize, sourceSize).begin());
            return successStatus();
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

    Types::WriteResult MemoryWriter::write(std::span<const std::byte> bytes) noexcept
    {
        if(!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .bytesWritten = 0};
        }

        if(bytes.empty())
        {
            return {.status = {}, .bytesWritten = 0};
        }

        const auto oldSize = bytes_.size();
        if(bytes.size() > bytes_.max_size() - oldSize)
        {
            return {.status = makeStatus(Types::ErrorCode::SizeLimitExceeded), .bytesWritten = 0};
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
                Types::Status appendStatus = appendAliasedBytes(bytes_, sourceOffset, bytes.size());
                if(!appendStatus.ok())
                {
                    return {.status = std::move(appendStatus), .bytesWritten = 0};
                }

                return {.status = {}, .bytesWritten = bytes.size()};
            }

            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());

            return {.status = {}, .bytesWritten = bytes.size()};
        }
        catch(const std::bad_alloc &)
        {
            return {.status = makeStatus(Types::ErrorCode::OutOfMemory), .bytesWritten = 0};
        }
        catch(const std::length_error &)
        {
            return {.status = makeStatus(Types::ErrorCode::SizeLimitExceeded), .bytesWritten = 0};
        }
        catch(...)
        {
            return {.status = makeStatus(Types::ErrorCode::Unknown), .bytesWritten = 0};
        }
    }

    Types::Status MemoryWriter::flush(Types::FlushMode mode) noexcept
    {
        if(!isValidFlushMode(mode))
        {
            return makeStatus(Types::ErrorCode::InvalidArgument);
        }

        if(!open_)
        {
            return makeStatus(Types::ErrorCode::NotOpen);
        }

        return successStatus();
    }

    Types::Status MemoryWriter::close() noexcept
    {
        open_ = false;
        return successStatus();
    }

    Types::PositionResult MemoryWriter::position() const noexcept
    {
        if(!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .position = 0};
        }

        return {.status = successStatus(), .position = static_cast<std::uint64_t>(bytes_.size())};
    }

    Types::Status MemoryWriter::reserve(std::size_t capacity) noexcept
    {
        if(capacity > bytes_.max_size())
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        try
        {
            bytes_.reserve(capacity);
            return successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return makeStatus(Types::ErrorCode::OutOfMemory);
        }
        catch(const std::length_error &)
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }
        catch(...)
        {
            return makeStatus(Types::ErrorCode::Unknown);
        }
    }

    std::span<const std::byte> MemoryWriter::bytes() const noexcept
    {
        return bytes_;
    }

    Types::CopyTextResult MemoryWriter::copyText() const noexcept
    {
        Types::CopyTextResult result;
        const auto view = bytes();

        if(view.empty())
        {
            return result;
        }

        if(view.size() > result.text.max_size())
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        const std::string_view textView(reinterpret_cast<const char *>(view.data()), view.size());

        if(Unicode::Utf8::validate(textView).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            result.status = makeStatus(Types::ErrorCode::EncodingFailed);
            return result;
        }

        try
        {
            result.text.assign(textView.data(), textView.size());
        }
        catch(const std::bad_alloc &)
        {
            result.status = makeStatus(Types::ErrorCode::OutOfMemory);
        }
        catch(const std::length_error &)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }
        catch(...)
        {
            result.status = makeStatus(Types::ErrorCode::Unknown);
        }

        return result;
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
