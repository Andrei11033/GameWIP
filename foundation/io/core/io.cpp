/// @file io.cpp
/// @brief Implements IO status helpers, default interfaces, memory streams, and whole-transfer algorithms.

#include "io/io.h"
#include "io/internal/io_test_hooks.h"

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
namespace GameWIP::IO::Detail::Core
{
    /// @brief Returns whether a reader capability failure should select the unknown-size path.
    /// @param status Status returned by a reader capability query.
    /// @return True when helpers may continue through the unknown-size path.
    [[nodiscard]] bool isUnsupportedReaderCapability(const Types::Status &status) noexcept
    {
        return status.code == Types::ErrorCode::NotSeekable || status.code == Types::ErrorCode::Unsupported;
    }

    /// @brief Result of probing a reader for an exact remaining byte count.
    struct KnownReadableByteCount
    {
        /// @brief Capability-query failure, or success when the count is known or intentionally unknown.
        Types::Status status;
        /// @brief Remaining bytes from the current position when known is true.
        std::uint64_t byteCount = 0;
        /// @brief Whether byteCount is authoritative for the next reads.
        bool known = false;
    };

    /// @brief Finds a known readable byte count when a reader exposes size and optionally position.
    /// @param reader Reader to query.
    /// @return Known remaining bytes, unknown success, or a capability-query failure.
    [[nodiscard]] KnownReadableByteCount knownReadableByteCount(Reader &reader) noexcept
    {
        KnownReadableByteCount result;

        Types::SizeResult sizeResult = reader.size();
        if (!sizeResult.status.ok())
        {
            if (isUnsupportedReaderCapability(sizeResult.status))
            {
                return result;
            }

            result.status = std::move(sizeResult.status);
            return result;
        }

        result.known = true;
        result.byteCount = sizeResult.sizeBytes;

        Types::PositionResult positionResult = reader.position();
        if (!positionResult.status.ok())
        {
            if (isUnsupportedReaderCapability(positionResult.status))
            {
                // A total size is not enough to preallocate the remaining range without a current
                // position, so fall back to streaming instead of assuming position zero.
                result.known = false;
                result.byteCount = 0;
                return result;
            }

            result.status = std::move(positionResult.status);
            result.known = false;
            result.byteCount = 0;
            return result;
        }

        if (positionResult.position > sizeResult.sizeBytes)
        {
            result.status = makeStatus(Types::ErrorCode::InvalidArgument);
            result.known = false;
            result.byteCount = 0;
            return result;
        }

        result.byteCount = sizeResult.sizeBytes - positionResult.position;
        return result;
    }

    /// @brief Reads a known number of bytes directly into the final output vector.
    /// @param reader Reader to drain.
    /// @param knownByteCount Known bytes remaining from the current reader position.
    /// @param maxBytes Caller byte limit.
    /// @return Collected bytes and final status.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytesKnownSize(Reader &reader, std::uint64_t knownByteCount, std::uint64_t maxBytes) noexcept
    {
        Types::ReadAllBytesResult result;

        if (knownByteCount > maxBytes)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        if (knownByteCount > static_cast<std::uint64_t>(result.bytes.max_size()))
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        if (knownByteCount == 0)
        {
            return result;
        }

        const auto expectedSize = static_cast<std::size_t>(knownByteCount);

        try
        {
#if INTERNAL_IO_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::ReadAllBytesStorage);
#endif
            result.bytes.resize(expectedSize);
        }
        catch (const std::bad_alloc &)
        {
            result.status = makeStatus(Types::ErrorCode::OutOfMemory);
            return result;
        }
        catch (const std::length_error &)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }
        catch (...)
        {
            result.status = makeStatus(Types::ErrorCode::Unknown);
            return result;
        }

        std::size_t totalRead = 0;

        while (totalRead < expectedSize)
        {
            const std::span<std::byte> destination(result.bytes.data() + totalRead, expectedSize - totalRead);
            Types::ReadResult readResult = reader.read(destination);

            if (readResult.bytesRead > destination.size())
            {
                result.bytes.resize(totalRead);
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }

            totalRead += readResult.bytesRead;

            if (!readResult.status.ok())
            {
                result.bytes.resize(totalRead);
                result.status = std::move(readResult.status);
                return result;
            }

            if (readResult.endOfStream && totalRead < expectedSize)
            {
                result.bytes.resize(totalRead);
                result.status = makeStatus(Types::ErrorCode::PartialRead);
                return result;
            }

            if (readResult.bytesRead == 0)
            {
                result.bytes.resize(totalRead);
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }
        }

        return result;
    }

    /// @brief Reads a known number of bytes directly into the final output string.
    /// @param reader Reader to drain.
    /// @param knownByteCount Known bytes remaining from the current reader position.
    /// @param maxBytes Caller byte limit.
    /// @return Collected text bytes and final status.
    [[nodiscard]] Types::ReadAllTextResult readAllTextKnownSize(Reader &reader, std::uint64_t knownByteCount, std::uint64_t maxBytes) noexcept
    {
        Types::ReadAllTextResult result;

        if (knownByteCount > maxBytes)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        if (knownByteCount > static_cast<std::uint64_t>(result.text.max_size()))
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        if (knownByteCount == 0)
        {
            return result;
        }

        const auto expectedSize = static_cast<std::size_t>(knownByteCount);

        try
        {
#if INTERNAL_IO_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::ReadAllTextStorage);
#endif
            result.text.resize(expectedSize);
        }
        catch (const std::bad_alloc &)
        {
            result.status = makeStatus(Types::ErrorCode::OutOfMemory);
            return result;
        }
        catch (const std::length_error &)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }
        catch (...)
        {
            result.status = makeStatus(Types::ErrorCode::Unknown);
            return result;
        }

        std::size_t totalRead = 0;

        while (totalRead < expectedSize)
        {
            const std::span<char> textDestination(result.text.data() + totalRead, expectedSize - totalRead);
            const std::span<std::byte> destination = std::as_writable_bytes(textDestination);
            Types::ReadResult readResult = reader.read(destination);

            if (readResult.bytesRead > destination.size())
            {
                result.text.resize(totalRead);
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }

            totalRead += readResult.bytesRead;

            if (!readResult.status.ok())
            {
                result.text.resize(totalRead);
                result.status = std::move(readResult.status);
                return result;
            }

            if (readResult.endOfStream && totalRead < expectedSize)
            {
                result.text.resize(totalRead);
                result.status = makeStatus(Types::ErrorCode::PartialRead);
                return result;
            }

            if (readResult.bytesRead == 0)
            {
                result.text.resize(totalRead);
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }
        }

        return result;
    }

    /// @brief Appends bytes to a vector without value-initializing the destination range first.
    /// @param destination Destination vector.
    /// @param source Source bytes to append.
    /// @return Success, SizeLimitExceeded for a representational limit, or OutOfMemory for allocation failure.
    [[nodiscard]] Types::Status appendBytes(
        std::vector<std::byte> &destination,
        std::span<const std::byte> source,
        bool injectReadAllFailure) noexcept
    {
        if (source.empty())
        {
            return successStatus();
        }

        if (source.size() > destination.max_size() - destination.size())
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        try
        {
#if INTERNAL_IO_TEST_HOOKS
            if (injectReadAllFailure)
            {
                ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::ReadAllBytesStorage);
            }
#else
            static_cast<void>(injectReadAllFailure);
#endif
            destination.insert(destination.end(), source.begin(), source.end());
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

        return successStatus();
    }

    /// @brief Appends bytes to a string without value-initializing the destination range first.
    /// @param destination Destination string.
    /// @param source Source bytes to append.
    /// @return Success, SizeLimitExceeded for a representational limit, or OutOfMemory for allocation failure.
    [[nodiscard]] Types::Status appendTextBytes(std::string &destination, std::span<const std::byte> source) noexcept
    {
        if (source.empty())
        {
            return successStatus();
        }

        if (source.size() > destination.max_size() - destination.size())
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        try
        {
#if INTERNAL_IO_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::ReadAllTextStorage);
#endif
            destination.append(reinterpret_cast<const char *>(source.data()), source.size());
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

        return successStatus();
    }

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

    /// @brief Consumes at most one byte to distinguish exact-limit EOF from over-limit input.
    /// @param reader Reader to probe.
    /// @return Success for end-of-stream, SizeLimitExceeded for more data, or the observed failure.
    [[nodiscard]] Types::Status probeForMoreData(Reader &reader) noexcept
    {
        std::byte probeByte;
        Types::ReadResult readResult = reader.read(std::span<std::byte>(&probeByte, 1));

        if (readResult.bytesRead > 1)
        {
            return makeStatus(Types::ErrorCode::ReadFailed);
        }

        if (readResult.bytesRead > 0)
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        if (!readResult.status.ok())
        {
            return std::move(readResult.status);
        }

        if (readResult.endOfStream)
        {
            return successStatus();
        }

        return makeStatus(Types::ErrorCode::ReadFailed);
    }

    /// @brief Reads unknown-size bytes through caller-provided scratch storage.
    /// @param reader Reader to drain.
    /// @param scratchBuffer Temporary transfer buffer. Must not be empty.
    /// @param maxBytes Caller byte limit.
    /// @return Collected bytes and final status.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytesWithScratch(
        Reader &reader,
        std::span<std::byte> scratchBuffer,
        std::uint64_t maxBytes) noexcept
    {
        Types::ReadAllBytesResult result;
        std::uint64_t totalRead = 0;

        while (true)
        {
            if (totalRead >= maxBytes)
            {
                result.status = probeForMoreData(reader);
                return result;
            }

            const auto remainingLimit = maxBytes - totalRead;
            const auto requestSize =
                static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(scratchBuffer.size()), remainingLimit));
            const std::span<std::byte> request = scratchBuffer.first(requestSize);
            Types::ReadResult readResult = reader.read(request);

            if (readResult.bytesRead > requestSize)
            {
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }

            if (readResult.bytesRead > 0)
            {
                Types::Status appendStatus = appendBytes(result.bytes, std::as_bytes(request.first(readResult.bytesRead)), true);
                if (!appendStatus.ok())
                {
                    result.status = std::move(appendStatus);
                    return result;
                }

                totalRead += readResult.bytesRead;
            }

            if (!readResult.status.ok())
            {
                result.status = std::move(readResult.status);
                return result;
            }

            if (readResult.endOfStream)
            {
                return result;
            }

            if (readResult.bytesRead == 0)
            {
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }
        }
    }

    /// @brief Reads unknown-size text bytes through caller-provided scratch storage.
    /// @param reader Reader to drain.
    /// @param scratchBuffer Temporary transfer buffer. Must not be empty.
    /// @param maxBytes Caller byte limit.
    /// @return Collected text bytes and final status.
    [[nodiscard]] Types::ReadAllTextResult readAllTextWithScratch(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes) noexcept
    {
        Types::ReadAllTextResult result;
        std::uint64_t totalRead = 0;

        while (true)
        {
            if (totalRead >= maxBytes)
            {
                result.status = probeForMoreData(reader);
                return result;
            }

            const auto remainingLimit = maxBytes - totalRead;
            const auto requestSize =
                static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(scratchBuffer.size()), remainingLimit));
            const std::span<std::byte> request = scratchBuffer.first(requestSize);
            Types::ReadResult readResult = reader.read(request);

            if (readResult.bytesRead > requestSize)
            {
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }

            if (readResult.bytesRead > 0)
            {
                Types::Status appendStatus = appendTextBytes(result.text, std::as_bytes(request.first(readResult.bytesRead)));
                if (!appendStatus.ok())
                {
                    result.status = std::move(appendStatus);
                    return result;
                }

                totalRead += readResult.bytesRead;
            }

            if (!readResult.status.ok())
            {
                result.status = std::move(readResult.status);
                return result;
            }

            if (readResult.endOfStream)
            {
                return result;
            }

            if (readResult.bytesRead == 0)
            {
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }
        }
    }
} // namespace GameWIP::IO::Detail::Core

namespace GameWIP::IO
{
    using namespace Detail::Core;

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

    MemoryReader::MemoryReader(std::string_view text) noexcept
        : MemoryReader(std::as_bytes(std::span<const char>(text.data(), text.size())))
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
#if INTERNAL_IO_TEST_HOOKS
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

            Types::Status appendStatus = appendBytes(bytes_, bytes, false);
            if (!appendStatus.ok())
            {
                return {.status = std::move(appendStatus), .bytesWritten = 0};
            }

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
#if INTERNAL_IO_TEST_HOOKS
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

    Types::TextCopyResult MemoryWriter::copyText() const noexcept
    {
        Types::TextCopyResult result;
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

        try
        {
#if INTERNAL_IO_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::MemoryWriterCopyText);
#endif
            result.text.assign(reinterpret_cast<const char *>(view.data()), view.size());
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

    Types::ReadAllBytesResult readAllBytes(Reader &reader, std::uint64_t maxBytes, std::size_t bufferSize) noexcept
    {
        if (bufferSize == 0)
        {
            return {.status = makeStatus(Types::ErrorCode::InvalidArgument)};
        }

        KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            return {.status = std::move(knownByteCount.status)};
        }

        if (knownByteCount.known)
        {
            return readAllBytesKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            return {.status = probeForMoreData(reader)};
        }

        const auto effectiveBufferSize = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(bufferSize), maxBytes));
        std::unique_ptr<std::byte[]> buffer;

        // The scratch bytes are immediately overwritten by Reader::read(), so avoid value-initializing them.
        try
        {
#if INTERNAL_IO_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::ReadAllScratchAllocation);
#endif
            buffer = std::make_unique_for_overwrite<std::byte[]>(effectiveBufferSize);
        }
        catch (const std::bad_alloc &)
        {
            return {.status = makeStatus(Types::ErrorCode::OutOfMemory)};
        }
        catch (const std::length_error &)
        {
            return {.status = makeStatus(Types::ErrorCode::SizeLimitExceeded)};
        }
        catch (...)
        {
            return {.status = makeStatus(Types::ErrorCode::Unknown)};
        }

        return readAllBytesWithScratch(reader, std::span<std::byte>(buffer.get(), effectiveBufferSize), maxBytes);
    }

    Types::ReadAllBytesResult readAllBytes(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes) noexcept
    {
        if (scratchBuffer.empty())
        {
            return {.status = makeStatus(Types::ErrorCode::InvalidArgument)};
        }

        KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            return {.status = std::move(knownByteCount.status)};
        }

        if (knownByteCount.known)
        {
            return readAllBytesKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            return {.status = probeForMoreData(reader)};
        }

        return readAllBytesWithScratch(reader, scratchBuffer, maxBytes);
    }

    Types::ReadAllTextResult readAllText(Reader &reader, std::uint64_t maxBytes, std::size_t bufferSize) noexcept
    {
        if (bufferSize == 0)
        {
            return {.status = makeStatus(Types::ErrorCode::InvalidArgument)};
        }

        KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            return {.status = std::move(knownByteCount.status)};
        }

        if (knownByteCount.known)
        {
            return readAllTextKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            return {.status = probeForMoreData(reader)};
        }

        const auto effectiveBufferSize = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(bufferSize), maxBytes));
        std::unique_ptr<std::byte[]> buffer;

        // The scratch bytes are immediately overwritten by Reader::read(), so avoid value-initializing them.
        try
        {
#if INTERNAL_IO_TEST_HOOKS
            ::GameWIP::IO::Detail::TestHooks::throwIfArmed(::GameWIP::IO::TestHooks::FailurePoint::ReadAllScratchAllocation);
#endif
            buffer = std::make_unique_for_overwrite<std::byte[]>(effectiveBufferSize);
        }
        catch (const std::bad_alloc &)
        {
            return {.status = makeStatus(Types::ErrorCode::OutOfMemory)};
        }
        catch (const std::length_error &)
        {
            return {.status = makeStatus(Types::ErrorCode::SizeLimitExceeded)};
        }
        catch (...)
        {
            return {.status = makeStatus(Types::ErrorCode::Unknown)};
        }

        return readAllTextWithScratch(reader, std::span<std::byte>(buffer.get(), effectiveBufferSize), maxBytes);
    }

    Types::ReadAllTextResult readAllText(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes) noexcept
    {
        if (scratchBuffer.empty())
        {
            return {.status = makeStatus(Types::ErrorCode::InvalidArgument)};
        }

        KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            return {.status = std::move(knownByteCount.status)};
        }

        if (knownByteCount.known)
        {
            return readAllTextKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            return {.status = probeForMoreData(reader)};
        }

        return readAllTextWithScratch(reader, scratchBuffer, maxBytes);
    }

    Types::WriteResult writeAllBytes(Writer &writer, std::span<const std::byte> bytes) noexcept
    {
        std::size_t totalWritten = 0;

        while (!bytes.empty())
        {
            auto writeResult = writer.write(bytes);

            if (writeResult.bytesWritten > bytes.size())
            {
                return {.status = makeStatus(Types::ErrorCode::WriteFailed), .bytesWritten = totalWritten};
            }

            totalWritten += writeResult.bytesWritten;

            if (!writeResult.status.ok())
            {
                return {.status = std::move(writeResult.status), .bytesWritten = totalWritten};
            }

            if (writeResult.bytesWritten == 0)
            {
                return {.status = makeStatus(Types::ErrorCode::WriteFailed), .bytesWritten = totalWritten};
            }

            bytes = bytes.subspan(writeResult.bytesWritten);
        }

        return {.status = successStatus(), .bytesWritten = totalWritten};
    }

    Types::WriteResult writeAllText(Writer &writer, std::string_view utf8Text) noexcept
    {
        return writeAllBytes(writer, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())));
    }
} // namespace GameWIP::IO
