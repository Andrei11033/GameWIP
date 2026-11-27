/// @file transfer.cpp
/// @brief Whole-stream transfer algorithms.

#include "io/transfer.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace GameWIP::IO::Internal::Core
{
    namespace ResultTypes = GameWIP::Status::Types;

    namespace
    {
        using Code = ResultTypes::Code;
        using Status = ResultTypes::Status;

        template <typename T> [[nodiscard]] ResultTypes::Result<T> failure(Code code) noexcept
        {
            return ResultTypes::Result<T>::failure(Status(code));
        }

        [[nodiscard]] bool isUnsupportedCapability(const Status &status) noexcept
        {
            return status.code() == Code::NotSeekable || status.code() == Code::Unsupported;
        }

        struct KnownReadableByteCount
        {
            std::uint64_t byteCount = 0;
            bool known = false;
        };

        [[nodiscard]] ResultTypes::Result<KnownReadableByteCount> knownReadableByteCount(Reader &reader) noexcept
        {
            const auto sizeResult = reader.size();

            if(!sizeResult.ok())
            {
                if(isUnsupportedCapability(sizeResult.status()))
                {
                    return ResultTypes::Result<KnownReadableByteCount>::success({});
                }

                return ResultTypes::Result<KnownReadableByteCount>::failure(sizeResult.status());
            }

            const auto positionResult = reader.position();

            if(!positionResult.ok())
            {
                if(isUnsupportedCapability(positionResult.status()))
                {
                    // Size alone cannot describe the remaining range.
                    return ResultTypes::Result<KnownReadableByteCount>::success({});
                }

                return ResultTypes::Result<KnownReadableByteCount>::failure(positionResult.status());
            }

            if(positionResult.value() > sizeResult.value())
            {
                return failure<KnownReadableByteCount>(Code::InvalidArgument);
            }

            return ResultTypes::Result<KnownReadableByteCount>::success({.byteCount = sizeResult.value() - positionResult.value(), .known = true});
        }

        [[nodiscard]] ResultTypes::Result<std::vector<std::byte>> readAllBytesKnownSize(
            Reader &reader,
            std::uint64_t knownByteCount,
            std::uint64_t maxBytes) noexcept
        {
            std::vector<std::byte> bytes;

            if(knownByteCount > maxBytes || knownByteCount > static_cast<std::uint64_t>(bytes.max_size()))
            {
                return failure<std::vector<std::byte>>(Code::SizeLimitExceeded);
            }

            if(knownByteCount == 0)
            {
                return ResultTypes::Result<std::vector<std::byte>>::success(std::move(bytes));
            }

            const auto expectedSize = static_cast<std::size_t>(knownByteCount);

            try
            {
                bytes.resize(expectedSize);
            }
            catch(const std::bad_alloc &)
            {
                return failure<std::vector<std::byte>>(Code::OutOfMemory);
            }
            catch(const std::length_error &)
            {
                return failure<std::vector<std::byte>>(Code::SizeLimitExceeded);
            }
            catch(...)
            {
                return failure<std::vector<std::byte>>(Code::Unknown);
            }

            std::size_t totalRead = 0;

            while(totalRead < expectedSize)
            {
                const std::span<std::byte> destination = std::span{bytes}.subspan(totalRead);
                auto readResult = reader.read(destination);

                if(!readResult.ok())
                {
                    const std::size_t reportedBytes = readResult.hasValue() ? readResult.value().bytesRead : 0;
                    if(reportedBytes > destination.size())
                    {
                        bytes.resize(totalRead);
                        return ResultTypes::Result<std::vector<std::byte>>::failure(Status(Code::ReadFailed), std::move(bytes));
                    }

                    totalRead += reportedBytes;
                    bytes.resize(totalRead);
                    return ResultTypes::Result<std::vector<std::byte>>::failure(readResult.status(), std::move(bytes));
                }

                const Types::ReadProgress progress = readResult.value();
                if(progress.bytesRead > destination.size())
                {
                    bytes.resize(totalRead);
                    return ResultTypes::Result<std::vector<std::byte>>::failure(Status(Code::ReadFailed), std::move(bytes));
                }

                totalRead += progress.bytesRead;

                if(progress.endOfStream && totalRead < expectedSize)
                {
                    bytes.resize(totalRead);
                    return ResultTypes::Result<std::vector<std::byte>>::failure(Status(Code::PartialRead), std::move(bytes));
                }

                if(progress.bytesRead == 0)
                {
                    bytes.resize(totalRead);
                    return ResultTypes::Result<std::vector<std::byte>>::failure(Status(Code::ReadFailed), std::move(bytes));
                }
            }

            return ResultTypes::Result<std::vector<std::byte>>::success(std::move(bytes));
        }

        [[nodiscard]] ResultTypes::Result<std::string> readAllTextKnownSize(
            Reader &reader,
            std::uint64_t knownByteCount,
            std::uint64_t maxBytes) noexcept
        {
            std::string text;

            if(knownByteCount > maxBytes || knownByteCount > static_cast<std::uint64_t>(text.max_size()))
            {
                return failure<std::string>(Code::SizeLimitExceeded);
            }

            if(knownByteCount == 0)
            {
                return ResultTypes::Result<std::string>::success(std::move(text));
            }

            const auto expectedSize = static_cast<std::size_t>(knownByteCount);

            try
            {
                text.resize(expectedSize);
            }
            catch(const std::bad_alloc &)
            {
                return failure<std::string>(Code::OutOfMemory);
            }
            catch(const std::length_error &)
            {
                return failure<std::string>(Code::SizeLimitExceeded);
            }
            catch(...)
            {
                return failure<std::string>(Code::Unknown);
            }

            std::size_t totalRead = 0;

            while(totalRead < expectedSize)
            {
                const std::span<char> textDestination = std::span{text}.subspan(totalRead);
                const std::span<std::byte> destination = std::as_writable_bytes(textDestination);
                auto readResult = reader.read(destination);

                if(!readResult.ok())
                {
                    const std::size_t reportedBytes = readResult.hasValue() ? readResult.value().bytesRead : 0;
                    if(reportedBytes > destination.size())
                    {
                        text.resize(totalRead);
                        return ResultTypes::Result<std::string>::failure(Status(Code::ReadFailed), std::move(text));
                    }

                    totalRead += reportedBytes;
                    text.resize(totalRead);
                    return ResultTypes::Result<std::string>::failure(readResult.status(), std::move(text));
                }

                const Types::ReadProgress progress = readResult.value();
                if(progress.bytesRead > destination.size())
                {
                    text.resize(totalRead);
                    return ResultTypes::Result<std::string>::failure(Status(Code::ReadFailed), std::move(text));
                }

                totalRead += progress.bytesRead;

                if(progress.endOfStream && totalRead < expectedSize)
                {
                    text.resize(totalRead);
                    return ResultTypes::Result<std::string>::failure(Status(Code::PartialRead), std::move(text));
                }

                if(progress.bytesRead == 0)
                {
                    text.resize(totalRead);
                    return ResultTypes::Result<std::string>::failure(Status(Code::ReadFailed), std::move(text));
                }
            }

            return ResultTypes::Result<std::string>::success(std::move(text));
        }

        [[nodiscard]] ResultTypes::Result<void> appendBytes(std::vector<std::byte> &destination, std::span<const std::byte> source) noexcept
        {
            if(source.empty())
            {
                return ResultTypes::Result<void>::success();
            }

            if(source.size() > destination.max_size() - destination.size())
            {
                return ResultTypes::Result<void>::failure(Status(Code::SizeLimitExceeded));
            }

            try
            {
                destination.insert(destination.end(), source.begin(), source.end());
            }
            catch(const std::bad_alloc &)
            {
                return ResultTypes::Result<void>::failure(Status(Code::OutOfMemory));
            }
            catch(const std::length_error &)
            {
                return ResultTypes::Result<void>::failure(Status(Code::SizeLimitExceeded));
            }
            catch(...)
            {
                return ResultTypes::Result<void>::failure(Status(Code::Unknown));
            }

            return ResultTypes::Result<void>::success();
        }

        [[nodiscard]] ResultTypes::Result<void> appendText(std::string &destination, std::span<const std::byte> source) noexcept
        {
            if(source.empty())
            {
                return ResultTypes::Result<void>::success();
            }

            if(source.size() > destination.max_size() - destination.size())
            {
                return ResultTypes::Result<void>::failure(Status(Code::SizeLimitExceeded));
            }

            try
            {
                destination.append(reinterpret_cast<const char *>(source.data()), source.size());
            }
            catch(const std::bad_alloc &)
            {
                return ResultTypes::Result<void>::failure(Status(Code::OutOfMemory));
            }
            catch(const std::length_error &)
            {
                return ResultTypes::Result<void>::failure(Status(Code::SizeLimitExceeded));
            }
            catch(...)
            {
                return ResultTypes::Result<void>::failure(Status(Code::Unknown));
            }

            return ResultTypes::Result<void>::success();
        }

        [[nodiscard]] ResultTypes::Result<void> probeForMoreData(Reader &reader) noexcept
        {
            std::byte probeByte;
            const auto readResult = reader.read(std::span<std::byte>(&probeByte, 1));

            if(readResult.hasValue() && readResult.value().bytesRead > 1)
            {
                return ResultTypes::Result<void>::failure(Status(Code::ReadFailed));
            }

            if(readResult.hasValue() && readResult.value().bytesRead > 0)
            {
                return ResultTypes::Result<void>::failure(Status(Code::SizeLimitExceeded));
            }

            if(!readResult.ok())
            {
                return ResultTypes::Result<void>::failure(readResult.status());
            }

            if(readResult.value().endOfStream)
            {
                return ResultTypes::Result<void>::success();
            }

            return ResultTypes::Result<void>::failure(Status(Code::ReadFailed));
        }

        struct ReadScratchBuffer
        {
            std::unique_ptr<std::byte[]> storage;
            std::span<std::byte> bytes;
        };

        [[nodiscard]] ResultTypes::Result<ReadScratchBuffer> makeReadScratchBuffer(std::size_t size) noexcept
        {
            ReadScratchBuffer buffer;

            try
            {
                buffer.storage = std::make_unique_for_overwrite<std::byte[]>(size);
            }
            catch(const std::bad_alloc &)
            {
                return failure<ReadScratchBuffer>(Code::OutOfMemory);
            }
            catch(const std::length_error &)
            {
                return failure<ReadScratchBuffer>(Code::SizeLimitExceeded);
            }
            catch(...)
            {
                return failure<ReadScratchBuffer>(Code::Unknown);
            }

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            buffer.bytes = std::span<std::byte>(buffer.storage.get(), size);
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
            return ResultTypes::Result<ReadScratchBuffer>::success(std::move(buffer));
        }

        [[nodiscard]] ResultTypes::Result<std::vector<std::byte>> readAllBytesWithScratch(
            Reader &reader,
            std::span<std::byte> scratchBuffer,
            std::uint64_t maxBytes) noexcept
        {
            std::vector<std::byte> bytes;
            std::uint64_t totalRead = 0;

            while(true)
            {
                if(totalRead >= maxBytes)
                {
                    const auto probe = probeForMoreData(reader);
                    if(probe.ok())
                    {
                        return ResultTypes::Result<std::vector<std::byte>>::success(std::move(bytes));
                    }
                    return ResultTypes::Result<std::vector<std::byte>>::failure(probe.status(), std::move(bytes));
                }

                const auto remainingLimit = maxBytes - totalRead;
                const auto requestSize =
                    static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(scratchBuffer.size()), remainingLimit));
                const std::span<std::byte> request = scratchBuffer.first(requestSize);
                auto readResult = reader.read(request);
                const std::size_t reportedBytes = readResult.hasValue() ? readResult.value().bytesRead : 0;

                if(reportedBytes > requestSize)
                {
                    return ResultTypes::Result<std::vector<std::byte>>::failure(Status(Code::ReadFailed), std::move(bytes));
                }

                if(reportedBytes > 0)
                {
                    auto appendResult = appendBytes(bytes, std::as_bytes(request.first(reportedBytes)));
                    if(!appendResult.ok())
                    {
                        return ResultTypes::Result<std::vector<std::byte>>::failure(appendResult.status(), std::move(bytes));
                    }
                    totalRead += reportedBytes;
                }

                if(!readResult.ok())
                {
                    return ResultTypes::Result<std::vector<std::byte>>::failure(readResult.status(), std::move(bytes));
                }

                if(readResult.value().endOfStream)
                {
                    return ResultTypes::Result<std::vector<std::byte>>::success(std::move(bytes));
                }

                if(reportedBytes == 0)
                {
                    return ResultTypes::Result<std::vector<std::byte>>::failure(Status(Code::ReadFailed), std::move(bytes));
                }
            }
        }

        [[nodiscard]] ResultTypes::Result<std::string> readAllTextWithScratch(
            Reader &reader,
            std::span<std::byte> scratchBuffer,
            std::uint64_t maxBytes) noexcept
        {
            std::string text;
            std::uint64_t totalRead = 0;

            while(true)
            {
                if(totalRead >= maxBytes)
                {
                    const auto probe = probeForMoreData(reader);
                    if(probe.ok())
                    {
                        return ResultTypes::Result<std::string>::success(std::move(text));
                    }
                    return ResultTypes::Result<std::string>::failure(probe.status(), std::move(text));
                }

                const auto remainingLimit = maxBytes - totalRead;
                const auto requestSize =
                    static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(scratchBuffer.size()), remainingLimit));
                const std::span<std::byte> request = scratchBuffer.first(requestSize);
                auto readResult = reader.read(request);
                const std::size_t reportedBytes = readResult.hasValue() ? readResult.value().bytesRead : 0;

                if(reportedBytes > requestSize)
                {
                    return ResultTypes::Result<std::string>::failure(Status(Code::ReadFailed), std::move(text));
                }

                if(reportedBytes > 0)
                {
                    auto appendResult = appendText(text, std::as_bytes(request.first(reportedBytes)));
                    if(!appendResult.ok())
                    {
                        return ResultTypes::Result<std::string>::failure(appendResult.status(), std::move(text));
                    }
                    totalRead += reportedBytes;
                }

                if(!readResult.ok())
                {
                    return ResultTypes::Result<std::string>::failure(readResult.status(), std::move(text));
                }

                if(readResult.value().endOfStream)
                {
                    return ResultTypes::Result<std::string>::success(std::move(text));
                }

                if(reportedBytes == 0)
                {
                    return ResultTypes::Result<std::string>::failure(Status(Code::ReadFailed), std::move(text));
                }
            }
        }

        [[nodiscard]] ResultTypes::Result<std::string> finalizeReadAllText(ResultTypes::Result<std::string> result) noexcept
        {
            if(!result.hasValue())
            {
                return result;
            }

            std::string text = std::move(result).value();
            const auto validation = Unicode::Utf8::validate(text);

            if(validation.outcome == Unicode::Types::ValidationOutcome::Valid)
            {
                if(result.ok())
                {
                    return ResultTypes::Result<std::string>::success(std::move(text));
                }
                return ResultTypes::Result<std::string>::failure(result.status(), std::move(text));
            }

            text.resize(validation.validPrefixBytes);
            const bool reachedDefinitiveEnd = result.ok() || result.status().code() == Code::PartialRead;

            if(validation.outcome == Unicode::Types::ValidationOutcome::InvalidEncoding || reachedDefinitiveEnd)
            {
                return ResultTypes::Result<std::string>::failure(Status(Code::EncodingFailed), std::move(text));
            }

            return ResultTypes::Result<std::string>::failure(result.status(), std::move(text));
        }
    } // namespace
} // namespace GameWIP::IO::Internal::Core

namespace GameWIP::IO
{
    namespace ResultTypes = GameWIP::Status::Types;

    using namespace Internal::Core;

    [[nodiscard]] ResultTypes::Result<std::vector<std::byte>> readAllBytes(Reader &reader, std::uint64_t maxBytes, std::size_t bufferSize) noexcept
    {
        if(bufferSize == 0)
        {
            return ResultTypes::Result<std::vector<std::byte>>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        auto knownByteCount = knownReadableByteCount(reader);
        if(!knownByteCount.ok())
        {
            return ResultTypes::Result<std::vector<std::byte>>::failure(knownByteCount.status());
        }

        if(knownByteCount.value().known)
        {
            return readAllBytesKnownSize(reader, knownByteCount.value().byteCount, maxBytes);
        }

        if(maxBytes == 0)
        {
            auto probe = probeForMoreData(reader);
            return probe.ok() ? ResultTypes::Result<std::vector<std::byte>>::success({})
                              : ResultTypes::Result<std::vector<std::byte>>::failure(probe.status());
        }

        const auto effectiveBufferSize = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(bufferSize), maxBytes));
        auto scratch = makeReadScratchBuffer(effectiveBufferSize);
        if(!scratch.ok())
        {
            return ResultTypes::Result<std::vector<std::byte>>::failure(scratch.status());
        }

        return readAllBytesWithScratch(reader, scratch.value().bytes, maxBytes);
    }

    [[nodiscard]] ResultTypes::Result<std::vector<std::byte>> readAllBytes(
        Reader &reader,
        std::span<std::byte> scratchBuffer,
        std::uint64_t maxBytes) noexcept
    {
        if(scratchBuffer.empty())
        {
            return ResultTypes::Result<std::vector<std::byte>>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        auto knownByteCount = knownReadableByteCount(reader);
        if(!knownByteCount.ok())
        {
            return ResultTypes::Result<std::vector<std::byte>>::failure(knownByteCount.status());
        }

        if(knownByteCount.value().known)
        {
            return readAllBytesKnownSize(reader, knownByteCount.value().byteCount, maxBytes);
        }

        if(maxBytes == 0)
        {
            auto probe = probeForMoreData(reader);
            return probe.ok() ? ResultTypes::Result<std::vector<std::byte>>::success({})
                              : ResultTypes::Result<std::vector<std::byte>>::failure(probe.status());
        }

        return readAllBytesWithScratch(reader, scratchBuffer, maxBytes);
    }

    [[nodiscard]] ResultTypes::Result<std::string> readAllText(Reader &reader, std::uint64_t maxBytes, std::size_t bufferSize) noexcept
    {
        if(bufferSize == 0)
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        auto knownByteCount = knownReadableByteCount(reader);
        if(!knownByteCount.ok())
        {
            return ResultTypes::Result<std::string>::failure(knownByteCount.status());
        }

        if(knownByteCount.value().known)
        {
            return finalizeReadAllText(readAllTextKnownSize(reader, knownByteCount.value().byteCount, maxBytes));
        }

        if(maxBytes == 0)
        {
            auto probe = probeForMoreData(reader);
            if(probe.ok())
            {
                return ResultTypes::Result<std::string>::success({});
            }
            return ResultTypes::Result<std::string>::failure(probe.status());
        }

        const auto effectiveBufferSize = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(bufferSize), maxBytes));
        auto scratch = makeReadScratchBuffer(effectiveBufferSize);
        if(!scratch.ok())
        {
            return ResultTypes::Result<std::string>::failure(scratch.status());
        }

        return finalizeReadAllText(readAllTextWithScratch(reader, scratch.value().bytes, maxBytes));
    }

    [[nodiscard]] ResultTypes::Result<std::string> readAllText(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes) noexcept
    {
        if(scratchBuffer.empty())
        {
            return ResultTypes::Result<std::string>::failure(ResultTypes::Status(ResultTypes::Code::InvalidArgument));
        }

        auto knownByteCount = knownReadableByteCount(reader);
        if(!knownByteCount.ok())
        {
            return ResultTypes::Result<std::string>::failure(knownByteCount.status());
        }

        if(knownByteCount.value().known)
        {
            return finalizeReadAllText(readAllTextKnownSize(reader, knownByteCount.value().byteCount, maxBytes));
        }

        if(maxBytes == 0)
        {
            auto probe = probeForMoreData(reader);
            if(probe.ok())
            {
                return ResultTypes::Result<std::string>::success({});
            }
            return ResultTypes::Result<std::string>::failure(probe.status());
        }

        return finalizeReadAllText(readAllTextWithScratch(reader, scratchBuffer, maxBytes));
    }

    [[nodiscard]] ResultTypes::Result<Types::WriteProgress> writeAllBytes(Writer &writer, std::span<const std::byte> bytes) noexcept
    {
        std::size_t totalWritten = 0;

        while(!bytes.empty())
        {
            auto writeResult = writer.write(bytes);
            const std::size_t reportedBytes = writeResult.hasValue() ? writeResult.value().bytesWritten : 0;

            if(reportedBytes > bytes.size())
            {
                return ResultTypes::Result<Types::WriteProgress>::failure(
                    ResultTypes::Status(ResultTypes::Code::WriteFailed),
                    Types::WriteProgress{.bytesWritten = totalWritten});
            }
            totalWritten += reportedBytes;

            if(!writeResult.ok())
            {
                return ResultTypes::Result<Types::WriteProgress>::failure(writeResult.status(), Types::WriteProgress{.bytesWritten = totalWritten});
            }

            if(reportedBytes == 0)
            {
                return ResultTypes::Result<Types::WriteProgress>::failure(
                    ResultTypes::Status(ResultTypes::Code::WriteFailed),
                    Types::WriteProgress{.bytesWritten = totalWritten});
            }
            bytes = bytes.subspan(reportedBytes);
        }

        return ResultTypes::Result<Types::WriteProgress>::success({.bytesWritten = totalWritten});
    }

    [[nodiscard]] ResultTypes::Result<Types::WriteProgress> writeAllText(Writer &writer, std::string_view utf8Text) noexcept
    {
        if(Unicode::Utf8::validate(utf8Text).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return ResultTypes::Result<Types::WriteProgress>::failure(
                ResultTypes::Status(ResultTypes::Code::EncodingFailed),
                Types::WriteProgress{.bytesWritten = 0});
        }

        return writeAllBytes(writer, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())));
    }
} // namespace GameWIP::IO
