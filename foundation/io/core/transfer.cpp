/// @file transfer.cpp
/// @brief Implements IO status helpers, default interfaces, memory streams, and whole-transfer algorithms.

#include "io/transfer.h"
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
namespace GameWIP::IO::Detail::Core
{
    namespace
    {
        // Non-seekable readers are allowed to use the streaming path; other
        // capability failures must propagate instead of being hidden.
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

        /// @brief Validates collected text and trims any malformed or incomplete suffix.
        /// @param result Text-read result whose collected bytes may still be unvalidated.
        /// @return Result whose text field is always valid UTF-8.
        [[nodiscard]] Types::ReadAllTextResult finalizeReadAllText(Types::ReadAllTextResult result) noexcept
        {
            const Unicode::Types::Utf8::ValidationResult validation = Unicode::Utf8::validate(result.text);
            if (validation.outcome == Unicode::Types::ValidationOutcome::Valid)
            {
                return result;
            }

            result.text.resize(validation.validPrefixBytes);

            const bool reachedDefinitiveEnd = result.status.ok() || result.status.code == Types::ErrorCode::PartialRead;
            if (validation.outcome == Unicode::Types::ValidationOutcome::InvalidEncoding || reachedDefinitiveEnd)
            {
                result.status = makeStatus(Types::ErrorCode::EncodingFailed);
            }

            return result;
        }

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
                const std::span<std::byte> destination = std::span{result.bytes}.subspan(totalRead);
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
                const std::span<char> textDestination = std::span{result.text}.subspan(totalRead);
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

        // These helpers copy scratch data directly into their destinations so
        // the transfer path does not create a second temporary range.
        // Both public streaming entry points reject an empty scratch buffer
        // before reaching these helpers, so each read request can make progress.
        [[nodiscard]] Types::Status appendBytes(std::vector<std::byte> &destination, std::span<const std::byte> source) noexcept
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
                    Types::Status appendStatus = appendBytes(result.bytes, std::as_bytes(request.first(readResult.bytesRead)));
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

        [[nodiscard]] Types::ReadAllTextResult readAllTextWithScratch(
            Reader &reader,
            std::span<std::byte> scratchBuffer,
            std::uint64_t maxBytes) noexcept
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
    } // namespace
} // namespace GameWIP::IO::Detail::Core

namespace GameWIP::IO
{
    using namespace Detail::Core;

    // ------------------------------------------------------------
    // Whole-stream reads
    // ------------------------------------------------------------

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

        // effectiveBufferSize is the exact allocation count used for this operation-owned scratch array.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        const std::span<std::byte> scratch(buffer.get(), effectiveBufferSize);
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        return readAllBytesWithScratch(reader, scratch, maxBytes);
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
            return finalizeReadAllText(readAllTextKnownSize(reader, knownByteCount.byteCount, maxBytes));
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

        // effectiveBufferSize is the exact allocation count used for this operation-owned scratch array.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        const std::span<std::byte> scratch(buffer.get(), effectiveBufferSize);
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        return finalizeReadAllText(readAllTextWithScratch(reader, scratch, maxBytes));
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
            return finalizeReadAllText(readAllTextKnownSize(reader, knownByteCount.byteCount, maxBytes));
        }

        if (maxBytes == 0)
        {
            return {.status = probeForMoreData(reader)};
        }

        return finalizeReadAllText(readAllTextWithScratch(reader, scratchBuffer, maxBytes));
    }

    // ------------------------------------------------------------
    // Whole-stream writes
    // ------------------------------------------------------------

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
        if (Unicode::Utf8::validate(utf8Text).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return {.status = makeStatus(Types::ErrorCode::EncodingFailed), .bytesWritten = 0};
        }

        return writeAllBytes(writer, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())));
    }
} // namespace GameWIP::IO
