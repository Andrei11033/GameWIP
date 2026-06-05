/// @file io.cpp
/// @brief Core implementation for the GameWIP IO contract library.

#include "io/io.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace GameWIP::IO::Detail::Core
{
    /// @brief Returns whether a reader capability failure should be treated as unknown size/position.
    /// @param status Status returned by a reader capability query.
    /// @return True when helpers may continue through the unknown-size path.
    [[nodiscard]] bool isUnsupportedReaderCapability(const Types::Status &status) noexcept
    {
        return status.code == Types::ErrorCode::NotSeekable || status.code == Types::ErrorCode::Unsupported;
    }

    /// @brief Creates a failed whole-stream byte read result without allocating a message.
    /// @param code Error code to store.
    /// @return Read-all result with empty bytes and the requested failure code.
    [[nodiscard]] Types::ReadAllBytesResult makeReadAllBytesFailure(Types::ErrorCode code)
    {
        Types::ReadAllBytesResult result;
        result.status = makeStatus(code);
        return result;
    }

    /// @brief Creates a failed whole-stream text read result without allocating a message.
    /// @param code Error code to store.
    /// @return Read-all text result with empty text and the requested failure code.
    [[nodiscard]] Types::ReadAllTextResult makeReadAllTextFailure(Types::ErrorCode code)
    {
        Types::ReadAllTextResult result;
        result.status = makeStatus(code);
        return result;
    }

    struct KnownReadableByteCount
    {
        Types::Status status;
        std::uint64_t byteCount = 0;
        bool known = false;
    };

    /// @brief Finds a known readable byte count when a reader exposes size and optionally position.
    /// @param reader Reader to query.
    /// @return Known remaining bytes, unknown success, or a capability-query failure.
    [[nodiscard]] KnownReadableByteCount knownReadableByteCount(Reader &reader)
    {
        KnownReadableByteCount result;

        const Types::SizeResult sizeResult = reader.size();
        if (!sizeResult.status.ok())
        {
            if (isUnsupportedReaderCapability(sizeResult.status))
            {
                return result;
            }

            result.status = sizeResult.status;
            return result;
        }

        result.known = true;
        result.byteCount = sizeResult.sizeBytes;

        const Types::PositionResult positionResult = reader.position();
        if (!positionResult.status.ok())
        {
            if (isUnsupportedReaderCapability(positionResult.status))
            {
                result.known = false;
                result.byteCount = 0;
                return result;
            }

            result.status = positionResult.status;
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
    [[nodiscard]] Types::ReadAllBytesResult readAllBytesKnownSize(Reader &reader, std::uint64_t knownByteCount, std::uint64_t maxBytes)
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
            result.bytes.resize(expectedSize);
        }
        catch (const std::bad_alloc &)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        std::size_t totalRead = 0;

        while (totalRead < expectedSize)
        {
            const std::span<std::byte> destination(result.bytes.data() + totalRead, expectedSize - totalRead);
            const Types::ReadResult readResult = reader.read(destination);

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
                result.status = readResult.status;
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
    [[nodiscard]] Types::ReadAllTextResult readAllTextKnownSize(Reader &reader, std::uint64_t knownByteCount, std::uint64_t maxBytes)
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
            result.text.resize(expectedSize);
        }
        catch (const std::bad_alloc &)
        {
            result.status = makeStatus(Types::ErrorCode::SizeLimitExceeded);
            return result;
        }

        std::size_t totalRead = 0;

        while (totalRead < expectedSize)
        {
            const std::span<char> textDestination(result.text.data() + totalRead, expectedSize - totalRead);
            const std::span<std::byte> destination = std::as_writable_bytes(textDestination);
            const Types::ReadResult readResult = reader.read(destination);

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
                result.status = readResult.status;
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
    /// @return Success or SizeLimitExceeded when allocation fails.
    [[nodiscard]] Types::Status appendBytes(std::vector<std::byte> &destination, std::span<const std::byte> source)
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
            destination.insert(destination.end(), source.begin(), source.end());
        }
        catch (const std::bad_alloc &)
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        return successStatus();
    }

    /// @brief Appends bytes to a string without value-initializing the destination range first.
    /// @param destination Destination string.
    /// @param source Source bytes to append.
    /// @return Success or SizeLimitExceeded when allocation fails.
    [[nodiscard]] Types::Status appendTextBytes(std::string &destination, std::span<const std::byte> source)
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
            destination.append(reinterpret_cast<const char *>(source.data()), source.size());
        }
        catch (const std::bad_alloc &)
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        return successStatus();
    }

    /// @brief Appends a range already owned by a byte vector without allocating a temporary copy.
    /// @param destination Destination vector that also owns the source bytes.
    /// @param sourceOffset Byte offset of the source range in destination.
    /// @param sourceSize Number of source bytes to append.
    /// @return Success or SizeLimitExceeded when allocation fails.
    [[nodiscard]] Types::Status appendAliasedBytes(std::vector<std::byte> &destination, std::size_t sourceOffset, std::size_t sourceSize)
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

        try
        {
            destination.resize(oldSize + sourceSize);
        }
        catch (const std::bad_alloc &)
        {
            return makeStatus(Types::ErrorCode::SizeLimitExceeded);
        }

        std::memcpy(destination.data() + oldSize, destination.data() + sourceOffset, sourceSize);
        return successStatus();
    }

    /// @brief Probes an unknown-size reader after the caller byte limit has been collected.
    /// @param reader Reader to probe.
    /// @return Success for end-of-stream, SizeLimitExceeded for more data, or the observed failure.
    [[nodiscard]] Types::Status probeForMoreData(Reader &reader)
    {
        std::byte probeByte;
        const Types::ReadResult readResult = reader.read(std::span<std::byte>(&probeByte, 1));

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
            return readResult.status;
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
    [[nodiscard]] Types::ReadAllBytesResult readAllBytesWithScratch(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes)
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
            const Types::ReadResult readResult = reader.read(request);

            if (readResult.bytesRead > requestSize)
            {
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }

            if (readResult.bytesRead > 0)
            {
                const Types::Status appendStatus = appendBytes(result.bytes, std::as_bytes(request.first(readResult.bytesRead)));
                if (!appendStatus.ok())
                {
                    result.status = appendStatus;
                    return result;
                }

                totalRead += readResult.bytesRead;
            }

            if (!readResult.status.ok())
            {
                result.status = readResult.status;
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
    [[nodiscard]] Types::ReadAllTextResult readAllTextWithScratch(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes)
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
            const Types::ReadResult readResult = reader.read(request);

            if (readResult.bytesRead > requestSize)
            {
                result.status = makeStatus(Types::ErrorCode::ReadFailed);
                return result;
            }

            if (readResult.bytesRead > 0)
            {
                const Types::Status appendStatus = appendTextBytes(result.text, std::as_bytes(request.first(readResult.bytesRead)));
                if (!appendStatus.ok())
                {
                    result.status = appendStatus;
                    return result;
                }

                totalRead += readResult.bytesRead;
            }

            if (!readResult.status.ok())
            {
                result.status = readResult.status;
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

    Types::Status makeStatus(Types::ErrorCode code, std::int64_t nativeCode, std::string message)
    {
        return {.code = code, .nativeCode = nativeCode, .message = std::move(message)};
    }

    Types::Status successStatus()
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
        case Types::ErrorCode::DirectoryCreateFailed:
            return "DirectoryCreateFailed";
        case Types::ErrorCode::DirectoryListFailed:
            return "DirectoryListFailed";

        case Types::ErrorCode::PartialRead:
            return "PartialRead";
        case Types::ErrorCode::PartialWrite:
            return "PartialWrite";
        case Types::ErrorCode::SizeLimitExceeded:
            return "SizeLimitExceeded";

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

    Types::Status Reader::close()
    {
        return successStatus();
    }

    Types::PositionResult Reader::position() const
    {
        return {.status = makeStatus(Types::ErrorCode::NotSeekable), .position = 0};
    }

    Types::SizeResult Reader::size() const
    {
        return {.status = makeStatus(Types::ErrorCode::NotSeekable), .sizeBytes = 0};
    }

    Types::Status Reader::seek([[maybe_unused]] std::int64_t offset, [[maybe_unused]] Types::SeekOrigin origin)
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

    Types::Status Writer::flush([[maybe_unused]] Types::FlushMode mode)
    {
        return successStatus();
    }

    Types::Status Writer::close()
    {
        return successStatus();
    }

    Types::PositionResult Writer::position() const
    {
        return {.status = makeStatus(Types::ErrorCode::NotSeekable), .position = 0};
    }

    Types::Status Writer::seek([[maybe_unused]] std::int64_t offset, [[maybe_unused]] Types::SeekOrigin origin)
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

    Types::ReadResult MemoryReader::read(std::span<std::byte> destination)
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

    Types::Status MemoryReader::close()
    {
        open_ = false;
        return successStatus();
    }

    Types::PositionResult MemoryReader::position() const
    {
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .position = 0};
        }

        return {.status = {}, .position = static_cast<std::uint64_t>(position_)};
    }

    Types::SizeResult MemoryReader::size() const
    {
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .sizeBytes = 0};
        }

        return {.status = {}, .sizeBytes = static_cast<std::uint64_t>(bytes_.size())};
    }

    Types::Status MemoryReader::seek(std::int64_t offset, Types::SeekOrigin origin)
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

    MemoryWriter::MemoryWriter(std::size_t initialCapacity)
    {
        bytes_.reserve(initialCapacity);
    }

    bool MemoryWriter::isOpen() const noexcept
    {
        return open_;
    }

    bool MemoryWriter::canSeek() const noexcept
    {
        return false;
    }

    Types::WriteResult MemoryWriter::write(std::span<const std::byte> bytes)
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

        const std::byte *const inputBegin = bytes.data();
        const std::byte *const ownBegin = bytes_.data();
        const std::byte *const ownEnd = ownBegin == nullptr ? nullptr : ownBegin + oldSize;
        const std::less<const std::byte *> pointerLess;

        if (ownBegin != nullptr && !pointerLess(inputBegin, ownBegin) && pointerLess(inputBegin, ownEnd))
        {
            const auto sourceOffset = static_cast<std::size_t>(inputBegin - ownBegin);
            const Types::Status appendStatus = appendAliasedBytes(bytes_, sourceOffset, bytes.size());
            if (!appendStatus.ok())
            {
                return {.status = appendStatus, .bytesWritten = 0};
            }

            return {.status = {}, .bytesWritten = bytes.size()};
        }

        const Types::Status appendStatus = appendBytes(bytes_, bytes);
        if (!appendStatus.ok())
        {
            return {.status = appendStatus, .bytesWritten = 0};
        }

        return {.status = {}, .bytesWritten = bytes.size()};
    }

    Types::Status MemoryWriter::flush([[maybe_unused]] Types::FlushMode mode)
    {
        if (!open_)
        {
            return makeStatus(Types::ErrorCode::NotOpen);
        }

        return successStatus();
    }

    Types::Status MemoryWriter::close()
    {
        open_ = false;
        return successStatus();
    }

    Types::PositionResult MemoryWriter::position() const
    {
        if (!open_)
        {
            return {.status = makeStatus(Types::ErrorCode::NotOpen), .position = 0};
        }

        return {.status = successStatus(), .position = static_cast<std::uint64_t>(bytes_.size())};
    }

    void MemoryWriter::reserve(std::size_t capacity)
    {
        bytes_.reserve(capacity);
    }

    std::span<const std::byte> MemoryWriter::bytes() const noexcept
    {
        return std::span<const std::byte>(bytes_.data(), bytes_.size());
    }

    std::string MemoryWriter::text() const
    {
        const auto view = bytes();
        if (view.empty())
        {
            return {};
        }

        return std::string(reinterpret_cast<const char *>(view.data()), view.size());
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

    Types::ReadAllBytesResult readAllBytes(Reader &reader, std::uint64_t maxBytes, std::size_t bufferSize)
    {
        if (bufferSize == 0)
        {
            return makeReadAllBytesFailure(Types::ErrorCode::InvalidArgument);
        }

        const KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            Types::ReadAllBytesResult result;
            result.status = knownByteCount.status;
            return result;
        }

        if (knownByteCount.known)
        {
            return readAllBytesKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            Types::ReadAllBytesResult result;
            result.status = probeForMoreData(reader);
            return result;
        }

        const auto effectiveBufferSize = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(bufferSize), maxBytes));
        std::unique_ptr<std::byte[]> buffer;

        try
        {
            buffer = std::make_unique_for_overwrite<std::byte[]>(effectiveBufferSize);
        }
        catch (const std::bad_alloc &)
        {
            return makeReadAllBytesFailure(Types::ErrorCode::SizeLimitExceeded);
        }

        return readAllBytesWithScratch(reader, std::span<std::byte>(buffer.get(), effectiveBufferSize), maxBytes);
    }

    Types::ReadAllBytesResult readAllBytes(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes)
    {
        if (scratchBuffer.empty())
        {
            return makeReadAllBytesFailure(Types::ErrorCode::InvalidArgument);
        }

        const KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            Types::ReadAllBytesResult result;
            result.status = knownByteCount.status;
            return result;
        }

        if (knownByteCount.known)
        {
            return readAllBytesKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            Types::ReadAllBytesResult result;
            result.status = probeForMoreData(reader);
            return result;
        }

        return readAllBytesWithScratch(reader, scratchBuffer, maxBytes);
    }

    Types::ReadAllTextResult readAllText(Reader &reader, std::uint64_t maxBytes, std::size_t bufferSize)
    {
        if (bufferSize == 0)
        {
            return makeReadAllTextFailure(Types::ErrorCode::InvalidArgument);
        }

        const KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            Types::ReadAllTextResult result;
            result.status = knownByteCount.status;
            return result;
        }

        if (knownByteCount.known)
        {
            return readAllTextKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            Types::ReadAllTextResult result;
            result.status = probeForMoreData(reader);
            return result;
        }

        const auto effectiveBufferSize = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(bufferSize), maxBytes));
        std::unique_ptr<std::byte[]> buffer;

        try
        {
            buffer = std::make_unique_for_overwrite<std::byte[]>(effectiveBufferSize);
        }
        catch (const std::bad_alloc &)
        {
            return makeReadAllTextFailure(Types::ErrorCode::SizeLimitExceeded);
        }

        return readAllTextWithScratch(reader, std::span<std::byte>(buffer.get(), effectiveBufferSize), maxBytes);
    }

    Types::ReadAllTextResult readAllText(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes)
    {
        if (scratchBuffer.empty())
        {
            return makeReadAllTextFailure(Types::ErrorCode::InvalidArgument);
        }

        const KnownReadableByteCount knownByteCount = knownReadableByteCount(reader);
        if (!knownByteCount.status.ok())
        {
            Types::ReadAllTextResult result;
            result.status = knownByteCount.status;
            return result;
        }

        if (knownByteCount.known)
        {
            return readAllTextKnownSize(reader, knownByteCount.byteCount, maxBytes);
        }

        if (maxBytes == 0)
        {
            Types::ReadAllTextResult result;
            result.status = probeForMoreData(reader);
            return result;
        }

        return readAllTextWithScratch(reader, scratchBuffer, maxBytes);
    }

    Types::Status writeAllBytes(Writer &writer, std::span<const std::byte> bytes)
    {
        while (!bytes.empty())
        {
            const auto writeResult = writer.write(bytes);

            if (writeResult.bytesWritten > bytes.size())
            {
                return makeStatus(Types::ErrorCode::WriteFailed);
            }

            if (!writeResult.status.ok())
            {
                return writeResult.status;
            }

            if (writeResult.bytesWritten == 0)
            {
                return makeStatus(Types::ErrorCode::WriteFailed);
            }

            bytes = bytes.subspan(writeResult.bytesWritten);
        }

        return successStatus();
    }

    Types::Status writeAllText(Writer &writer, std::string_view utf8Text)
    {
        return writeAllBytes(writer, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())));
    }
} // namespace GameWIP::IO
