/// @file io.cpp
/// @brief Implements IO status helpers, default interfaces, memory streams, and whole-transfer algorithms.

#include "io/io.h"
#include "io/internal/io_test_hooks.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

/// @brief Internal algorithms shared by the public whole-stream and memory-writer implementations.
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

            if (sourceOffset > oldSize || sourceSize > oldSize - sourceOffset)
            {
                return makeStatus(Types::ErrorCode::InvalidArgument);
            }

            if (sourceSize > destination.max_size() - oldSize)
            {
                return makeStatus(Types::ErrorCode::SizeLimitExceeded);
            }

            // sourceOffset remains valid even if resize reallocates and invalidates the original span.
            try
            {
                destination.resize(oldSize + sourceSize);
            }
            catch (const std::bad_alloc &)
            {
                return makeStatus(Types::ErrorCode::OutOfMemory);
            }
            catch (const std::length_error &)
            {
                return makeStatus(Types::ErrorCode::SizeLimitExceeded);
            }
            catch (...)
            {
                return makeStatus(Types::ErrorCode::Unknown);
            }

            std::memcpy(destination.data() + oldSize, destination.data() + sourceOffset, sourceSize);
            return successStatus();
        }
    } // namespace

    Types::Status makeStatus(Types::ErrorCode code, std::int64_t nativeCode, std::string message) noexcept
    {
        return {.code = code, .nativeCode = nativeCode, .message = std::move(message)};
    }

    Types::Status successStatus() noexcept
    {
        return {};
    }

    std::string_view errorCodeName(Types::ErrorCode code) noexcept
    {
        switch (code)
        {
        case Types::ErrorCode::Success:
            return "Success";

        case Types::ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case Types::ErrorCode::Unsupported:
            return "Unsupported";
        case Types::ErrorCode::NotOpen:
            return "NotOpen";
        case Types::ErrorCode::AlreadyOpen:
            return "AlreadyOpen";

        case Types::ErrorCode::NotFound:
            return "NotFound";
        case Types::ErrorCode::AlreadyExists:
            return "AlreadyExists";
        case Types::ErrorCode::PermissionDenied:
            return "PermissionDenied";
        case Types::ErrorCode::PathTooLong:
            return "PathTooLong";

        case Types::ErrorCode::IsDirectory:
            return "IsDirectory";
        case Types::ErrorCode::NotDirectory:
            return "NotDirectory";
        case Types::ErrorCode::NotSeekable:
            return "NotSeekable";
        case Types::ErrorCode::EndOfStream:
            return "EndOfStream";

        case Types::ErrorCode::OpenFailed:
            return "OpenFailed";
        case Types::ErrorCode::ReadFailed:
            return "ReadFailed";
        case Types::ErrorCode::WriteFailed:
            return "WriteFailed";
        case Types::ErrorCode::FlushFailed:
            return "FlushFailed";
        case Types::ErrorCode::CloseFailed:
            return "CloseFailed";
        case Types::ErrorCode::SeekFailed:
            return "SeekFailed";
        case Types::ErrorCode::StatFailed:
            return "StatFailed";
        case Types::ErrorCode::RemoveFailed:
            return "RemoveFailed";
        case Types::ErrorCode::ReplaceFailed:
            return "ReplaceFailed";
        case Types::ErrorCode::CopyFailed:
            return "CopyFailed";
        case Types::ErrorCode::MoveFailed:
            return "MoveFailed";
        case Types::ErrorCode::ResizeFailed:
            return "ResizeFailed";
        case Types::ErrorCode::LockFailed:
            return "LockFailed";
        case Types::ErrorCode::UnlockFailed:
            return "UnlockFailed";
        case Types::ErrorCode::DirectoryCreateFailed:
            return "DirectoryCreateFailed";
        case Types::ErrorCode::DirectoryListFailed:
            return "DirectoryListFailed";
        case Types::ErrorCode::DirectoryNotEmpty:
            return "DirectoryNotEmpty";

        case Types::ErrorCode::PartialRead:
            return "PartialRead";
        case Types::ErrorCode::PartialWrite:
            return "PartialWrite";
        case Types::ErrorCode::SizeLimitExceeded:
            return "SizeLimitExceeded";
        case Types::ErrorCode::OutOfMemory:
            return "OutOfMemory";

        case Types::ErrorCode::ResourceBusy:
            return "ResourceBusy";
        case Types::ErrorCode::StorageFull:
            return "StorageFull";
        case Types::ErrorCode::BrokenPipe:
            return "BrokenPipe";
        case Types::ErrorCode::Interrupted:
            return "Interrupted";

        case Types::ErrorCode::EncodingFailed:
            return "EncodingFailed";
        case Types::ErrorCode::NativeFailure:
            return "NativeFailure";
        case Types::ErrorCode::Unknown:
            return "Unknown";
        }

        return "Unknown";
    }

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
        if (!isValidFlushMode(mode))
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

    Types::ReadResult MemoryReader::read(std::span<std::byte> destination) noexcept
    {
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .bytesRead = 0, .endOfStream = false};
        }

        if (position_ > bytes_.size())
        {
            return {.status = makeStatus(Types::ErrorCode::InvalidArgument), .bytesRead = 0, .endOfStream = false};
        }

        if (destination.empty())
        {
            return {.status = {}, .bytesRead = 0, .endOfStream = position_ >= bytes_.size()};
        }

        if (position_ >= bytes_.size())
        {
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        const auto available = bytes_.size() - position_;
        const auto count = std::min(destination.size(), available);

        std::memmove(destination.data(), bytes_.data() + position_, count);
        position_ += count;

        return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
    }

    Types::Status MemoryReader::close() noexcept
    {
        open_ = false;
        return successStatus();
    }

    Types::PositionResult MemoryReader::position() const noexcept
    {
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .position = 0};
        }

        return {.status = {}, .position = static_cast<std::uint64_t>(position_)};
    }

    Types::SizeResult MemoryReader::size() const noexcept
    {
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .sizeBytes = 0};
        }

        return {.status = {}, .sizeBytes = static_cast<std::uint64_t>(bytes_.size())};
    }

    Types::Status MemoryReader::seek(std::int64_t offset, Types::SeekOrigin origin) noexcept
    {
        if (!open_)
        {
            return makeStatus(Types::ErrorCode::NotOpen);
        }

        if (bytes_.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        const auto size = static_cast<std::int64_t>(bytes_.size());

        if (position_ > static_cast<std::size_t>(size))
        {
            return makeStatus(Types::ErrorCode::InvalidArgument);
        }

        std::int64_t base = 0;

        switch (origin)
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
            return makeStatus(Types::ErrorCode::InvalidArgument);
        }

        if (offset < -base || offset > size - base)
        {
            return makeStatus(Types::ErrorCode::SeekFailed);
        }

        const auto target = base + offset;
        position_ = static_cast<std::size_t>(target);

        return successStatus();
    }

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
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .bytesWritten = 0};
        }

        if (bytes.empty())
        {
            return {.status = {}, .bytesWritten = 0};
        }

        const auto oldSize = bytes_.size();

        if (bytes.size() > bytes_.max_size() - oldSize)
        {
            return {.status = makeStatus(Types::ErrorCode::SizeLimitExceeded), .bytesWritten = 0};
        }

        try
        {
#if IO_INTERNAL_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::MemoryWriterWrite);
#endif

            // Detect self-aliasing before appending can reallocate the vector. The source offset,
            // rather than the input pointer, remains usable after storage moves.
            const std::byte *const inputBegin = bytes.data();
            const std::byte *const ownBegin = bytes_.data();
            const std::byte *const ownEnd = ownBegin == nullptr ? nullptr : ownBegin + oldSize;
            const std::less<const std::byte *> pointerLess;

            if (ownBegin != nullptr && !pointerLess(inputBegin, ownBegin) && pointerLess(inputBegin, ownEnd))
            {
                const auto sourceOffset = static_cast<std::size_t>(inputBegin - ownBegin);
                Types::Status appendStatus = appendAliasedBytes(bytes_, sourceOffset, bytes.size());
                if (!appendStatus.ok())
                {
                    return {.status = std::move(appendStatus), .bytesWritten = 0};
                }

                return {.status = {}, .bytesWritten = bytes.size()};
            }

            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());

            return {.status = {}, .bytesWritten = bytes.size()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = makeStatus(Types::ErrorCode::OutOfMemory), .bytesWritten = 0};
        }
        catch (const std::length_error &)
        {
            return {.status = makeStatus(Types::ErrorCode::SizeLimitExceeded), .bytesWritten = 0};
        }
        catch (...)
        {
            return {.status = makeStatus(Types::ErrorCode::Unknown), .bytesWritten = 0};
        }
    }

    Types::Status MemoryWriter::flush(Types::FlushMode mode) noexcept
    {
        if (!isValidFlushMode(mode))
        {
            return makeStatus(Types::ErrorCode::InvalidArgument);
        }

        if (!open_)
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
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .position = 0};
        }

        return {.status = successStatus(), .position = static_cast<std::uint64_t>(bytes_.size())};
    }

    Types::Status MemoryWriter::reserve(std::size_t capacity) noexcept
    {
        if (capacity > bytes_.max_size())
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        try
        {
#if IO_INTERNAL_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::MemoryWriterReserve);
#endif
            bytes_.reserve(capacity);
            return successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return makeStatus(Types::ErrorCode::OutOfMemory);
        }
        catch (const std::length_error &)
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }
        catch (...)
        {
            return makeStatus(Types::ErrorCode::Unknown);
        }
    }

    std::span<const std::byte> MemoryWriter::bytes() const noexcept
    {
        return std::span<const std::byte>(bytes_.data(), bytes_.size());
    }

    Types::CopyTextResult MemoryWriter::copyText() const noexcept
    {
        Types::CopyTextResult result;
        const auto view = bytes();
        if (view.empty())
        {
            return result;
        }

        if (view.size() > result.text.max_size())
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        const std::string_view textView(reinterpret_cast<const char *>(view.data()), view.size());
        if (Unicode::Utf8::validate(textView).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            result.status = makeStatus(Types::ErrorCode::EncodingFailed);
            return result;
        }

        try
        {
#if IO_INTERNAL_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::MemoryWriterCopyText);
#endif
            result.text.assign(textView.data(), textView.size());
        }
        catch (const std::bad_alloc &)
        {
            result.status = makeStatus(Types::ErrorCode::OutOfMemory);
        }
        catch (const std::length_error &)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }
        catch (...)
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
