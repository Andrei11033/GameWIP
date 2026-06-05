/// @file io_test.cpp
/// @brief Executable self-tests for the GameWIP IO library.

#include "test/io_test.h"

#include "io/io.h"
#include "test_support/test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;

    using IOTestOptions = GameWIP::Test::IOTestOptions;
    using ErrorCode = IO::Types::ErrorCode;

    [[nodiscard]] IO::Types::Status statusWith(ErrorCode code)
    {
        return IO::makeStatus(code);
    }

    [[nodiscard]] std::vector<std::byte> makeBytes(std::initializer_list<unsigned int> values)
    {
        std::vector<std::byte> bytes;
        bytes.reserve(values.size());

        for (const unsigned int value : values)
        {
            bytes.push_back(static_cast<std::byte>(value));
        }

        return bytes;
    }

    [[nodiscard]] std::vector<std::byte> copyBytes(std::span<const std::byte> bytes)
    {
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::span<const std::byte> spanOf(const std::vector<std::byte> &bytes) noexcept
    {
        return std::span<const std::byte>(bytes.data(), bytes.size());
    }

    [[nodiscard]] std::span<const std::byte> bytesOf(std::string_view text)
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    class MinimalReader final : public IO::Reader
    {
    public:
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) override
        {
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }
    };

    class UnknownSizeChunkReader final : public IO::Reader
    {
    public:
        UnknownSizeChunkReader(std::span<const std::byte> bytes, std::size_t maxChunkSize, bool reportEndWithLastRead = true)
            : bytes_(bytes)
            , maxChunkSize_(maxChunkSize)
            , reportEndWithLastRead_(reportEndWithLastRead)
        {
        }

        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            if (destination.empty())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = position_ >= bytes_.size()};
            }

            if (position_ >= bytes_.size())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = true};
            }

            const std::size_t remaining = bytes_.size() - position_;
            const std::size_t count = std::min(destination.size(), std::min(maxChunkSize_, remaining));
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;

            return {.status = {}, .bytesRead = count, .endOfStream = reportEndWithLastRead_ && position_ == bytes_.size()};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t maxChunkSize_ = 1;
        bool reportEndWithLastRead_ = true;
        std::size_t position_ = 0;
    };

    class SizeWithoutPositionReader final : public IO::Reader
    {
    public:
        SizeWithoutPositionReader(std::span<const std::byte> bytes, std::size_t initialPosition)
            : bytes_(bytes)
            , position_(initialPosition)
        {
        }

        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            if (position_ >= bytes_.size())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = true};
            }

            const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = {}, .sizeBytes = static_cast<std::uint64_t>(bytes_.size())};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ = 0;
    };

    class ReportedSizeReader final : public IO::Reader
    {
    public:
        ReportedSizeReader(std::span<const std::byte> bytes, std::uint64_t reportedSize)
            : bytes_(bytes)
            , reportedSize_(reportedSize)
        {
        }

        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            if (position_ >= bytes_.size())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = true};
            }

            const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
        }

        [[nodiscard]] IO::Types::PositionResult position() const override
        {
            return {.status = {}, .position = static_cast<std::uint64_t>(position_)};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = {}, .sizeBytes = reportedSize_};
        }

    private:
        std::span<const std::byte> bytes_;
        std::uint64_t reportedSize_ = 0;
        std::size_t position_ = 0;
    };

    class SizeQueryFailureReader final : public IO::Reader
    {
    public:
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) override
        {
            readCalled_ = true;
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = statusWith(ErrorCode::PermissionDenied), .sizeBytes = 0};
        }

        [[nodiscard]] bool readCalled() const noexcept
        {
            return readCalled_;
        }

    private:
        bool readCalled_ = false;
    };

    class PositionQueryFailureReader final : public IO::Reader
    {
    public:
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) override
        {
            readCalled_ = true;
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        [[nodiscard]] IO::Types::PositionResult position() const override
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied), .position = 0};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = {}, .sizeBytes = 1};
        }

        [[nodiscard]] bool readCalled() const noexcept
        {
            return readCalled_;
        }

    private:
        bool readCalled_ = false;
    };

    class ImpossiblePositionReader final : public IO::Reader
    {
    public:
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) override
        {
            readCalled_ = true;
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        [[nodiscard]] IO::Types::PositionResult position() const override
        {
            return {.status = {}, .position = 2};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = {}, .sizeBytes = 1};
        }

        [[nodiscard]] bool readCalled() const noexcept
        {
            return readCalled_;
        }

    private:
        bool readCalled_ = false;
    };

    class ZeroProgressKnownSizeReader final : public IO::Reader
    {
    public:
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) override
        {
            return {.status = {}, .bytesRead = 0, .endOfStream = false};
        }

        [[nodiscard]] IO::Types::PositionResult position() const override
        {
            return {.status = {}, .position = 0};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = {}, .sizeBytes = 1};
        }
    };

    class UnsupportedSizeReader final : public IO::Reader
    {
    public:
        explicit UnsupportedSizeReader(std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            if (position_ >= bytes_.size())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = true};
            }

            const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = IO::makeStatus(ErrorCode::Unsupported), .sizeBytes = 0};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ = 0;
    };

    class ProbeFailureReader final : public IO::Reader
    {
    public:
        explicit ProbeFailureReader(std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            if (position_ >= bytes_.size())
            {
                return {.status = IO::makeStatus(ErrorCode::PermissionDenied), .bytesRead = 0, .endOfStream = false};
            }

            const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            return {.status = {}, .bytesRead = count, .endOfStream = false};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ = 0;
    };

    class InvalidReadCountReader final : public IO::Reader
    {
    public:
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            return {.status = {}, .bytesRead = destination.size() + 1, .endOfStream = false};
        }
    };

    class FailingKnownSizeReader final : public IO::Reader
    {
    public:
        FailingKnownSizeReader(std::span<const std::byte> bytes, std::size_t failAfterBytes, ErrorCode failureCode)
            : bytes_(bytes)
            , failAfterBytes_(failAfterBytes)
            , failureCode_(failureCode)
        {
        }

        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) override
        {
            if (destination.empty())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = position_ >= bytes_.size()};
            }

            if (position_ >= bytes_.size())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = true};
            }

            if (position_ >= failAfterBytes_)
            {
                const std::size_t count = std::min<std::size_t>(destination.size(), 1);
                std::copy_n(bytes_.data() + position_, count, destination.data());
                position_ += count;
                return {.status = statusWith(failureCode_), .bytesRead = count, .endOfStream = false};
            }

            const std::size_t remainingBeforeFailure = failAfterBytes_ - position_;
            const std::size_t count = std::min(destination.size(), remainingBeforeFailure);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
        }

        [[nodiscard]] IO::Types::PositionResult position() const override
        {
            return {.status = {}, .position = static_cast<std::uint64_t>(position_)};
        }

        [[nodiscard]] IO::Types::SizeResult size() const override
        {
            return {.status = {}, .sizeBytes = static_cast<std::uint64_t>(bytes_.size())};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t failAfterBytes_ = 0;
        ErrorCode failureCode_ = ErrorCode::ReadFailed;
        std::size_t position_ = 0;
    };

    class ChunkedWriter final : public IO::Writer
    {
    public:
        explicit ChunkedWriter(std::size_t maxChunkSize)
            : maxChunkSize_(maxChunkSize)
        {
        }

        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) override
        {
            if (bytes.empty())
            {
                return {.status = {}, .bytesWritten = 0};
            }

            if (maxChunkSize_ == 0)
            {
                return {.status = {}, .bytesWritten = 0};
            }

            const std::size_t count = std::min(maxChunkSize_, bytes.size());
            const std::span<const std::byte> accepted = bytes.first(count);
            bytes_.insert(bytes_.end(), accepted.begin(), accepted.end());
            return {.status = {}, .bytesWritten = count};
        }

        [[nodiscard]] const std::vector<std::byte> &bytes() const noexcept
        {
            return bytes_;
        }

    private:
        std::size_t maxChunkSize_ = 0;
        std::vector<std::byte> bytes_;
    };

    class FailingWriter final : public IO::Writer
    {
    public:
        explicit FailingWriter(ErrorCode code)
            : code_(code)
        {
        }

        [[nodiscard]] IO::Types::WriteResult write([[maybe_unused]] std::span<const std::byte> bytes) override
        {
            return {.status = statusWith(code_), .bytesWritten = 0};
        }

    private:
        ErrorCode code_ = ErrorCode::WriteFailed;
    };

    class InvalidWriteCountWriter final : public IO::Writer
    {
    public:
        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) override
        {
            return {.status = {}, .bytesWritten = bytes.size() + 1};
        }
    };

    static_assert(std::is_nothrow_move_constructible_v<MinimalReader>);
    static_assert(std::is_nothrow_move_assignable_v<MinimalReader>);
    static_assert(!std::is_copy_constructible_v<MinimalReader>);
    static_assert(!std::is_copy_assignable_v<MinimalReader>);
    static_assert(std::is_nothrow_move_constructible_v<ChunkedWriter>);
    static_assert(std::is_nothrow_move_assignable_v<ChunkedWriter>);
    static_assert(!std::is_copy_constructible_v<ChunkedWriter>);
    static_assert(!std::is_copy_assignable_v<ChunkedWriter>);
    static_assert(std::is_nothrow_move_constructible_v<IO::MemoryReader>);
    static_assert(std::is_nothrow_move_assignable_v<IO::MemoryReader>);
    static_assert(std::is_nothrow_move_constructible_v<IO::MemoryWriter>);
    static_assert(std::is_nothrow_move_assignable_v<IO::MemoryWriter>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::string &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::string &&>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &&>);

    void testErrorCodeNames(TestSupport::Context &context)
    {
        struct ErrorCodeName
        {
            ErrorCode code;
            std::string_view name;
        };

        constexpr std::array names{
            ErrorCodeName{ErrorCode::Success, "Success"},
            ErrorCodeName{ErrorCode::InvalidArgument, "InvalidArgument"},
            ErrorCodeName{ErrorCode::Unsupported, "Unsupported"},
            ErrorCodeName{ErrorCode::NotOpen, "NotOpen"},
            ErrorCodeName{ErrorCode::AlreadyOpen, "AlreadyOpen"},
            ErrorCodeName{ErrorCode::NotFound, "NotFound"},
            ErrorCodeName{ErrorCode::AlreadyExists, "AlreadyExists"},
            ErrorCodeName{ErrorCode::PermissionDenied, "PermissionDenied"},
            ErrorCodeName{ErrorCode::PathTooLong, "PathTooLong"},
            ErrorCodeName{ErrorCode::IsDirectory, "IsDirectory"},
            ErrorCodeName{ErrorCode::NotDirectory, "NotDirectory"},
            ErrorCodeName{ErrorCode::NotSeekable, "NotSeekable"},
            ErrorCodeName{ErrorCode::EndOfStream, "EndOfStream"},
            ErrorCodeName{ErrorCode::OpenFailed, "OpenFailed"},
            ErrorCodeName{ErrorCode::ReadFailed, "ReadFailed"},
            ErrorCodeName{ErrorCode::WriteFailed, "WriteFailed"},
            ErrorCodeName{ErrorCode::FlushFailed, "FlushFailed"},
            ErrorCodeName{ErrorCode::CloseFailed, "CloseFailed"},
            ErrorCodeName{ErrorCode::SeekFailed, "SeekFailed"},
            ErrorCodeName{ErrorCode::StatFailed, "StatFailed"},
            ErrorCodeName{ErrorCode::RemoveFailed, "RemoveFailed"},
            ErrorCodeName{ErrorCode::ReplaceFailed, "ReplaceFailed"},
            ErrorCodeName{ErrorCode::CopyFailed, "CopyFailed"},
            ErrorCodeName{ErrorCode::DirectoryCreateFailed, "DirectoryCreateFailed"},
            ErrorCodeName{ErrorCode::DirectoryListFailed, "DirectoryListFailed"},
            ErrorCodeName{ErrorCode::PartialRead, "PartialRead"},
            ErrorCodeName{ErrorCode::PartialWrite, "PartialWrite"},
            ErrorCodeName{ErrorCode::SizeLimitExceeded, "SizeLimitExceeded"},
            ErrorCodeName{ErrorCode::ResourceBusy, "ResourceBusy"},
            ErrorCodeName{ErrorCode::StorageFull, "StorageFull"},
            ErrorCodeName{ErrorCode::BrokenPipe, "BrokenPipe"},
            ErrorCodeName{ErrorCode::Interrupted, "Interrupted"},
            ErrorCodeName{ErrorCode::EncodingFailed, "EncodingFailed"},
            ErrorCodeName{ErrorCode::NativeFailure, "NativeFailure"},
            ErrorCodeName{ErrorCode::Unknown, "Unknown"},
        };

        for (const ErrorCodeName &entry : names)
        {
            static_cast<void>(context.expectEq(std::format("errorCodeName returns {}", entry.name), entry.name, IO::errorCodeName(entry.code)));
        }

        static_cast<void>(context.expectEq(
            "errorCodeName maps unknown enumerators to Unknown",
            std::string_view{"Unknown"},
            IO::errorCodeName(static_cast<ErrorCode>(-1))));
    }

    void testStatusAndDefaultContracts(TestSupport::Context &context)
    {
        const IO::Types::Status success = IO::successStatus();
        static_cast<void>(context.expectTrue("successStatus returns success", success.ok()));
        static_cast<void>(context.expectEq("successStatus native code defaults to zero", std::int64_t{0}, success.nativeCode));
        static_cast<void>(context.expectTrue("successStatus message defaults to empty", success.message.empty()));

        const IO::Types::Status failure = IO::makeStatus(ErrorCode::ReadFailed, 42, "read failed");
        static_cast<void>(context.expectFalse("makeStatus failure is not ok", failure.ok()));
        static_cast<void>(context.expectEq("makeStatus preserves portable code", ErrorCode::ReadFailed, failure.code));
        static_cast<void>(context.expectEq("makeStatus preserves native code", std::int64_t{42}, failure.nativeCode));
        static_cast<void>(context.expectEq("makeStatus preserves message", std::string{"read failed"}, failure.message));

        MinimalReader reader;
        static_cast<void>(context.expectTrue("Reader isOpen defaults to true", reader.isOpen()));
        static_cast<void>(context.expectFalse("Reader canSeek defaults to false", reader.canSeek()));
        static_cast<void>(context.expectTrue("Reader close default succeeds", reader.close().ok()));
        static_cast<void>(context.expectTrue("Stateless Reader remains open after default close", reader.isOpen()));
        static_cast<void>(context.expectEq("Reader position defaults to NotSeekable", ErrorCode::NotSeekable, reader.position().status.code));
        static_cast<void>(context.expectEq("Reader size defaults to NotSeekable", ErrorCode::NotSeekable, reader.size().status.code));
        static_cast<void>(
            context.expectEq("Reader seek defaults to NotSeekable", ErrorCode::NotSeekable, reader.seek(0, IO::Types::SeekOrigin::Begin).code));

        ChunkedWriter writer(1);
        static_cast<void>(context.expectTrue("Writer isOpen defaults to true", writer.isOpen()));
        static_cast<void>(context.expectFalse("Writer canSeek defaults to false", writer.canSeek()));
        static_cast<void>(context.expectTrue("Writer flush default succeeds", writer.flush().ok()));
        static_cast<void>(context.expectTrue("Writer close default succeeds", writer.close().ok()));
        static_cast<void>(context.expectTrue("Stateless Writer remains open after default close", writer.isOpen()));
        static_cast<void>(context.expectEq("Writer position defaults to NotSeekable", ErrorCode::NotSeekable, writer.position().status.code));
        static_cast<void>(
            context.expectEq("Writer seek defaults to NotSeekable", ErrorCode::NotSeekable, writer.seek(0, IO::Types::SeekOrigin::Begin).code));
    }

    void testMemoryReader(TestSupport::Context &context)
    {
        const std::vector<std::byte> source = makeBytes({0x41, 0x00, 0x42, 0xff});
        IO::MemoryReader reader(source);
        static_cast<void>(context.expectTrue("MemoryReader starts open", reader.isOpen()));
        static_cast<void>(context.expectTrue("MemoryReader can seek while open", reader.canSeek()));

        std::array<std::byte, 2> firstChunk{};
        const IO::Types::ReadResult firstRead = reader.read(firstChunk);
        static_cast<void>(context.expectTrue("MemoryReader first read succeeds", firstRead.status.ok()));
        static_cast<void>(context.expectEq("MemoryReader first read byte count", std::size_t{2}, firstRead.bytesRead));
        static_cast<void>(context.expectFalse("MemoryReader first read not EOS", firstRead.endOfStream));
        static_cast<void>(context.expectEq(
            "MemoryReader preserves first binary chunk",
            makeBytes({0x41, 0x00}),
            copyBytes(std::span<const std::byte>(firstChunk))));

        const IO::Types::PositionResult position = reader.position();
        static_cast<void>(context.expectTrue("MemoryReader position succeeds", position.status.ok()));
        static_cast<void>(context.expectEq("MemoryReader position reports current position", std::uint64_t{2}, position.position));

        const IO::Types::SizeResult size = reader.size();
        static_cast<void>(context.expectTrue("MemoryReader size succeeds", size.status.ok()));
        static_cast<void>(context.expectEq("MemoryReader size reports byte count", std::uint64_t{4}, size.sizeBytes));

        std::array<std::byte, 8> finalChunk{};
        const IO::Types::ReadResult finalRead = reader.read(finalChunk);
        static_cast<void>(context.expectTrue("MemoryReader final read succeeds", finalRead.status.ok()));
        static_cast<void>(context.expectEq("MemoryReader final read byte count", std::size_t{2}, finalRead.bytesRead));
        static_cast<void>(context.expectTrue("MemoryReader final read reports EOS", finalRead.endOfStream));
        static_cast<void>(context.expectEq(
            "MemoryReader preserves final binary chunk",
            makeBytes({0x42, 0xff}),
            copyBytes(std::span<const std::byte>(finalChunk.data(), finalRead.bytesRead))));

        const IO::Types::ReadResult eosRead = reader.read(finalChunk);
        static_cast<void>(context.expectTrue("MemoryReader EOS read succeeds", eosRead.status.ok()));
        static_cast<void>(context.expectEq("MemoryReader EOS read returns zero bytes", std::size_t{0}, eosRead.bytesRead));
        static_cast<void>(context.expectTrue("MemoryReader EOS read remains EOS", eosRead.endOfStream));

        const IO::Types::ReadResult emptyRead = reader.read(std::span<std::byte>{});
        static_cast<void>(context.expectTrue("MemoryReader empty read succeeds", emptyRead.status.ok()));
        static_cast<void>(context.expectTrue("MemoryReader empty read reports EOS at end", emptyRead.endOfStream));

        IO::MemoryReader textReader("abc");
        const IO::Types::ReadAllTextResult textResult = IO::readAllText(textReader);
        static_cast<void>(context.expectTrue("MemoryReader string view read succeeds", textResult.status.ok()));
        static_cast<void>(context.expectEq("MemoryReader string view preserves text", std::string{"abc"}, textResult.text));

        std::vector<std::byte> overlapSource = makeBytes({0x10, 0x20, 0x30, 0x40});
        IO::MemoryReader overlapReader(overlapSource);
        const IO::Types::ReadResult overlapRead = overlapReader.read(std::span<std::byte>(overlapSource).subspan(1, 3));
        static_cast<void>(context.expectTrue("MemoryReader overlapping read succeeds", overlapRead.status.ok()));
        static_cast<void>(
            context.expectEq("MemoryReader overlapping read preserves source order", makeBytes({0x10, 0x10, 0x20, 0x30}), overlapSource));

        static_cast<void>(context.expectTrue("MemoryReader close succeeds", reader.close().ok()));
        static_cast<void>(context.expectFalse("MemoryReader reports closed state", reader.isOpen()));
        static_cast<void>(context.expectFalse("MemoryReader cannot seek after close", reader.canSeek()));
        static_cast<void>(context.expectEq("MemoryReader read after close reports NotOpen", ErrorCode::NotOpen, reader.read(firstChunk).status.code));
        static_cast<void>(context.expectEq("MemoryReader position after close reports NotOpen", ErrorCode::NotOpen, reader.position().status.code));
        static_cast<void>(context.expectEq("MemoryReader size after close reports NotOpen", ErrorCode::NotOpen, reader.size().status.code));
        static_cast<void>(
            context.expectEq("MemoryReader seek after close reports NotOpen", ErrorCode::NotOpen, reader.seek(0, IO::Types::SeekOrigin::Begin).code));
        static_cast<void>(context.expectTrue("MemoryReader repeated close succeeds", reader.close().ok()));
    }

    void testMemoryReaderSeek(TestSupport::Context &context)
    {
        const std::vector<std::byte> source = makeBytes({0x10, 0x20, 0x30, 0x40});
        IO::MemoryReader reader(spanOf(source));

        static_cast<void>(context.expectTrue("MemoryReader seek from begin succeeds", reader.seek(2, IO::Types::SeekOrigin::Begin).ok()));
        static_cast<void>(context.expectEq("MemoryReader seek begin position", std::uint64_t{2}, reader.position().position));

        std::array<std::byte, 1> byte{};
        static_cast<void>(reader.read(byte));
        static_cast<void>(
            context.expectEq("MemoryReader read after seek returns target byte", makeBytes({0x30}), copyBytes(std::span<const std::byte>(byte))));

        static_cast<void>(context.expectTrue("MemoryReader seek from current succeeds", reader.seek(-1, IO::Types::SeekOrigin::Current).ok()));
        static_cast<void>(reader.read(byte));
        static_cast<void>(context.expectEq(
            "MemoryReader read after current seek returns same byte",
            makeBytes({0x30}),
            copyBytes(std::span<const std::byte>(byte))));

        static_cast<void>(context.expectTrue("MemoryReader seek from end succeeds", reader.seek(-1, IO::Types::SeekOrigin::End).ok()));
        static_cast<void>(reader.read(byte));
        static_cast<void>(
            context.expectEq("MemoryReader read after end seek returns last byte", makeBytes({0x40}), copyBytes(std::span<const std::byte>(byte))));

        static_cast<void>(context.expectEq(
            "MemoryReader rejects negative seek before beginning",
            ErrorCode::SeekFailed,
            reader.seek(-5, IO::Types::SeekOrigin::Begin).code));
        static_cast<void>(
            context.expectEq("MemoryReader rejects seek after end", ErrorCode::SeekFailed, reader.seek(1, IO::Types::SeekOrigin::End).code));
        static_cast<void>(context.expectEq(
            "MemoryReader rejects invalid seek origin",
            ErrorCode::InvalidArgument,
            reader.seek(0, static_cast<IO::Types::SeekOrigin>(99)).code));
    }

    void testMemoryWriter(TestSupport::Context &context)
    {
        IO::MemoryWriter writer(8);
        const std::vector<std::byte> source = makeBytes({0x01, 0x00, 0x02, 0xff});
        static_cast<void>(context.expectTrue("MemoryWriter starts open", writer.isOpen()));
        static_cast<void>(context.expectFalse("MemoryWriter is append-only", writer.canSeek()));
        static_cast<void>(context.expectTrue("MemoryWriter initial capacity is reserved", writer.capacity() >= 8));
        static_cast<void>(context.expectEq("MemoryWriter initial position is zero", std::uint64_t{0}, writer.position().position));
        static_cast<void>(
            context.expectEq("MemoryWriter seek reports NotSeekable", ErrorCode::NotSeekable, writer.seek(0, IO::Types::SeekOrigin::Begin).code));

        const IO::Types::WriteResult write = writer.write(source);
        static_cast<void>(context.expectTrue("MemoryWriter write succeeds", write.status.ok()));
        static_cast<void>(context.expectEq("MemoryWriter write byte count", source.size(), write.bytesWritten));
        static_cast<void>(context.expectEq("MemoryWriter size reports bytes written", source.size(), writer.size()));
        static_cast<void>(
            context.expectEq("MemoryWriter position reports bytes written", static_cast<std::uint64_t>(source.size()), writer.position().position));
        static_cast<void>(context.expectFalse("MemoryWriter no longer empty", writer.empty()));
        static_cast<void>(context.expectEq("MemoryWriter preserves binary data", source, copyBytes(writer.bytes())));

        const IO::Types::WriteResult emptyWrite = writer.write(std::span<const std::byte>{});
        static_cast<void>(context.expectTrue("MemoryWriter empty write succeeds", emptyWrite.status.ok()));
        static_cast<void>(context.expectEq("MemoryWriter empty write accepts zero bytes", std::size_t{0}, emptyWrite.bytesWritten));

        const std::size_t capacityBeforeClear = writer.capacity();
        writer.clear();
        static_cast<void>(context.expectTrue("MemoryWriter clear leaves writer empty", writer.empty()));
        static_cast<void>(context.expectEq("MemoryWriter clear resets size", std::size_t{0}, writer.size()));
        static_cast<void>(context.expectEq("MemoryWriter clear resets position", std::uint64_t{0}, writer.position().position));
        static_cast<void>(context.expectEq("MemoryWriter clear preserves capacity", capacityBeforeClear, writer.capacity()));

        writer.reserve(32);
        static_cast<void>(context.expectTrue("MemoryWriter reserve grows capacity", writer.capacity() >= 32));

        IO::MemoryWriter textWriter;
        static_cast<void>(IO::writeAllText(textWriter, std::string_view{"a\0b", 3}));
        static_cast<void>(context.expectEq("MemoryWriter text preserves NUL bytes", std::string("a\0b", 3), textWriter.text()));
        IO::MemoryWriter movedWriter(std::move(textWriter));
        static_cast<void>(context.expectEq("MemoryWriter move preserves bytes", std::string("a\0b", 3), movedWriter.text()));
        static_cast<void>(context.expectTrue("MemoryWriter move preserves open state", movedWriter.isOpen()));

        IO::MemoryWriter emptyTextWriter;
        static_cast<void>(context.expectTrue("MemoryWriter empty text extraction is empty", emptyTextWriter.text().empty()));

        IO::MemoryWriter aliasWriter(source.size());
        static_cast<void>(aliasWriter.write(source));
        const IO::Types::WriteResult aliasWrite = aliasWriter.write(aliasWriter.bytes().subspan(1, 2));
        static_cast<void>(context.expectTrue("MemoryWriter aliased write succeeds", aliasWrite.status.ok()));
        static_cast<void>(context.expectEq("MemoryWriter aliased write reports byte count", std::size_t{2}, aliasWrite.bytesWritten));
        static_cast<void>(context.expectEq(
            "MemoryWriter aliased write preserves source bytes",
            makeBytes({0x01, 0x00, 0x02, 0xff, 0x00, 0x02}),
            copyBytes(aliasWriter.bytes())));

        const std::vector<std::byte> takenBytes = aliasWriter.takeBytes();
        static_cast<void>(
            context.expectEq("MemoryWriter takeBytes returns collected bytes", makeBytes({0x01, 0x00, 0x02, 0xff, 0x00, 0x02}), takenBytes));
        static_cast<void>(context.expectTrue("MemoryWriter takeBytes leaves writer empty", aliasWriter.empty()));
        static_cast<void>(context.expectTrue("MemoryWriter remains open after takeBytes", aliasWriter.isOpen()));
        static_cast<void>(context.expectEq("MemoryWriter takeBytes resets position", std::uint64_t{0}, aliasWriter.position().position));

        static_cast<void>(context.expectTrue("MemoryWriter close succeeds", writer.close().ok()));
        static_cast<void>(context.expectFalse("MemoryWriter reports closed state", writer.isOpen()));
        static_cast<void>(context.expectFalse("MemoryWriter remains non-seekable after close", writer.canSeek()));
        static_cast<void>(context.expectEq("MemoryWriter write after close reports NotOpen", ErrorCode::NotOpen, writer.write(source).status.code));
        static_cast<void>(context.expectEq("MemoryWriter flush after close reports NotOpen", ErrorCode::NotOpen, writer.flush().code));
        static_cast<void>(context.expectEq("MemoryWriter position after close reports NotOpen", ErrorCode::NotOpen, writer.position().status.code));
        static_cast<void>(context.expectTrue("MemoryWriter repeated close succeeds", writer.close().ok()));
    }

    void testReadAllBytes(TestSupport::Context &context)
    {
        const std::vector<std::byte> source = makeBytes({0xde, 0xad, 0x00, 0xbe, 0xef});

        IO::MemoryReader reader(spanOf(source));
        const IO::Types::ReadAllBytesResult result = IO::readAllBytes(reader, IO::kNoByteLimit, 2);
        static_cast<void>(context.expectTrue("readAllBytes succeeds", result.status.ok()));
        static_cast<void>(context.expectEq("readAllBytes preserves bytes", source, result.bytes));

        IO::MemoryReader exactLimitReader(spanOf(source));
        const IO::Types::ReadAllBytesResult exactLimit = IO::readAllBytes(exactLimitReader, source.size(), 2);
        static_cast<void>(context.expectTrue("readAllBytes accepts exact byte limit", exactLimit.status.ok()));
        static_cast<void>(context.expectEq("readAllBytes exact limit preserves bytes", source, exactLimit.bytes));

        IO::MemoryReader seekedReader(spanOf(source));
        static_cast<void>(seekedReader.seek(2, IO::Types::SeekOrigin::Begin));
        const IO::Types::ReadAllBytesResult seekedResult = IO::readAllBytes(seekedReader, 3, 2);
        static_cast<void>(context.expectTrue("readAllBytes known remaining after seek succeeds", seekedResult.status.ok()));
        static_cast<void>(
            context.expectEq("readAllBytes known remaining after seek preserves bytes", makeBytes({0x00, 0xbe, 0xef}), seekedResult.bytes));

        IO::MemoryReader shortLimitReader(spanOf(source));
        const IO::Types::ReadAllBytesResult shortLimit = IO::readAllBytes(shortLimitReader, source.size() - 1, 2);
        static_cast<void>(context.expectEq("readAllBytes rejects known size beyond limit", ErrorCode::SizeLimitExceeded, shortLimit.status.code));
        static_cast<void>(context.expectTrue("readAllBytes limit failure returns no bytes", shortLimit.bytes.empty()));

        IO::MemoryReader zeroLimitReader(spanOf(source));
        const IO::Types::ReadAllBytesResult zeroLimit = IO::readAllBytes(zeroLimitReader, 0, 2);
        static_cast<void>(
            context.expectEq("readAllBytes zero limit rejects non-empty known stream", ErrorCode::SizeLimitExceeded, zeroLimit.status.code));
        static_cast<void>(context.expectTrue("readAllBytes zero limit returns empty bytes", zeroLimit.bytes.empty()));

        const std::vector<std::byte> emptySource;
        IO::MemoryReader emptyReader(spanOf(emptySource));
        const IO::Types::ReadAllBytesResult emptyResult = IO::readAllBytes(emptyReader, IO::kNoByteLimit, 2);
        static_cast<void>(context.expectTrue("readAllBytes empty known size succeeds", emptyResult.status.ok()));
        static_cast<void>(context.expectTrue("readAllBytes empty known size returns empty bytes", emptyResult.bytes.empty()));

        IO::MemoryReader emptyZeroLimitReader(spanOf(emptySource));
        const IO::Types::ReadAllBytesResult emptyZeroLimit = IO::readAllBytes(emptyZeroLimitReader, 0, 2);
        static_cast<void>(context.expectTrue("readAllBytes zero limit accepts empty known stream", emptyZeroLimit.status.ok()));

        std::array<std::byte, 2> scratch{};
        UnknownSizeChunkReader scratchReader(spanOf(source), 1);
        const IO::Types::ReadAllBytesResult scratchResult = IO::readAllBytes(scratchReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectTrue("readAllBytes scratch overload succeeds", scratchResult.status.ok()));
        static_cast<void>(context.expectEq("readAllBytes scratch overload preserves bytes", source, scratchResult.bytes));

        UnknownSizeChunkReader exactScratchLimitReader(spanOf(source), 2, false);
        const IO::Types::ReadAllBytesResult exactScratchLimit =
            IO::readAllBytes(exactScratchLimitReader, std::span<std::byte>(scratch), source.size());
        static_cast<void>(context.expectTrue("readAllBytes accepts exact unknown-size limit after EOF probe", exactScratchLimit.status.ok()));
        static_cast<void>(context.expectEq("readAllBytes exact unknown-size limit preserves bytes", source, exactScratchLimit.bytes));

        ProbeFailureReader probeFailureReader(spanOf(source));
        const IO::Types::ReadAllBytesResult probeFailure = IO::readAllBytes(probeFailureReader, std::span<std::byte>(scratch), source.size());
        static_cast<void>(context.expectEq("readAllBytes propagates limit probe failure", ErrorCode::PermissionDenied, probeFailure.status.code));
        static_cast<void>(context.expectEq("readAllBytes limit probe failure preserves limited bytes", source, probeFailure.bytes));

        UnknownSizeChunkReader scratchLimitReader(spanOf(source), 2);
        const IO::Types::ReadAllBytesResult scratchLimit = IO::readAllBytes(scratchLimitReader, std::span<std::byte>(scratch), 3);
        static_cast<void>(
            context.expectEq("readAllBytes rejects unknown-size stream beyond limit", ErrorCode::SizeLimitExceeded, scratchLimit.status.code));
        static_cast<void>(context.expectEq("readAllBytes scratch overload returns limited bytes", makeBytes({0xde, 0xad, 0x00}), scratchLimit.bytes));

        UnknownSizeChunkReader zeroLimitUnknownReader(spanOf(source), 1);
        const IO::Types::ReadAllBytesResult zeroLimitUnknown = IO::readAllBytes(zeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
        static_cast<void>(
            context.expectEq("readAllBytes zero limit rejects non-empty unknown stream", ErrorCode::SizeLimitExceeded, zeroLimitUnknown.status.code));
        static_cast<void>(context.expectTrue("readAllBytes zero unknown limit returns no bytes", zeroLimitUnknown.bytes.empty()));

        UnknownSizeChunkReader emptyZeroLimitUnknownReader(spanOf(emptySource), 1);
        const IO::Types::ReadAllBytesResult emptyZeroLimitUnknown = IO::readAllBytes(emptyZeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
        static_cast<void>(context.expectTrue("readAllBytes zero limit accepts empty unknown stream", emptyZeroLimitUnknown.status.ok()));

        UnknownSizeChunkReader emptyScratchReader(spanOf(source), 1);
        const IO::Types::ReadAllBytesResult emptyScratch = IO::readAllBytes(emptyScratchReader, std::span<std::byte>{});
        static_cast<void>(context.expectEq("readAllBytes rejects empty scratch", ErrorCode::InvalidArgument, emptyScratch.status.code));

        SizeWithoutPositionReader sizeWithoutPositionReader(spanOf(source), 2);
        const IO::Types::ReadAllBytesResult sizeWithoutPosition = IO::readAllBytes(sizeWithoutPositionReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectTrue("readAllBytes falls back when size is known but position is not", sizeWithoutPosition.status.ok()));
        static_cast<void>(
            context.expectEq("readAllBytes size-only fallback preserves remaining bytes", makeBytes({0x00, 0xbe, 0xef}), sizeWithoutPosition.bytes));

        ReportedSizeReader shortKnownReader(spanOf(source), source.size() + 2);
        const IO::Types::ReadAllBytesResult shortKnown = IO::readAllBytes(shortKnownReader, IO::kNoByteLimit, 2);
        static_cast<void>(
            context.expectEq("readAllBytes reports early known-size EOF as PartialRead", ErrorCode::PartialRead, shortKnown.status.code));
        static_cast<void>(context.expectEq("readAllBytes partial read preserves bytes", source, shortKnown.bytes));

        FailingKnownSizeReader failingReader(spanOf(source), 2, ErrorCode::PermissionDenied);
        const IO::Types::ReadAllBytesResult failingResult = IO::readAllBytes(failingReader, IO::kNoByteLimit, 2);
        static_cast<void>(context.expectEq("readAllBytes direct fill propagates failure", ErrorCode::PermissionDenied, failingResult.status.code));
        static_cast<void>(
            context.expectEq("readAllBytes direct fill keeps bytes before failure", makeBytes({0xde, 0xad, 0x00}), failingResult.bytes));

        SizeQueryFailureReader queryFailureReader;
        const IO::Types::ReadAllBytesResult queryFailure = IO::readAllBytes(queryFailureReader, IO::kNoByteLimit, 2);
        static_cast<void>(context.expectEq("readAllBytes propagates size query failure", ErrorCode::PermissionDenied, queryFailure.status.code));
        static_cast<void>(context.expectFalse("readAllBytes does not read after size query failure", queryFailureReader.readCalled()));

        PositionQueryFailureReader positionQueryFailureReader;
        const IO::Types::ReadAllBytesResult positionQueryFailure = IO::readAllBytes(positionQueryFailureReader, IO::kNoByteLimit, 2);
        static_cast<void>(
            context.expectEq("readAllBytes propagates position query failure", ErrorCode::PermissionDenied, positionQueryFailure.status.code));
        static_cast<void>(context.expectFalse("readAllBytes does not read after position query failure", positionQueryFailureReader.readCalled()));

        ImpossiblePositionReader impossiblePositionReader;
        const IO::Types::ReadAllBytesResult impossiblePosition = IO::readAllBytes(impossiblePositionReader, IO::kNoByteLimit, 2);
        static_cast<void>(context.expectEq("readAllBytes rejects position beyond size", ErrorCode::InvalidArgument, impossiblePosition.status.code));
        static_cast<void>(context.expectFalse("readAllBytes does not read after impossible position", impossiblePositionReader.readCalled()));

        UnsupportedSizeReader unsupportedSizeReader(spanOf(source));
        const IO::Types::ReadAllBytesResult unsupportedSize = IO::readAllBytes(unsupportedSizeReader, IO::kNoByteLimit, 2);
        static_cast<void>(context.expectTrue("readAllBytes falls back when size is Unsupported", unsupportedSize.status.ok()));
        static_cast<void>(context.expectEq("readAllBytes Unsupported fallback preserves bytes", source, unsupportedSize.bytes));

        InvalidReadCountReader invalidCountReader;
        const IO::Types::ReadAllBytesResult invalidCount = IO::readAllBytes(invalidCountReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectEq("readAllBytes rejects invalid reader byte count", ErrorCode::ReadFailed, invalidCount.status.code));

        UnknownSizeChunkReader zeroProgressReader(spanOf(source), 0);
        const IO::Types::ReadAllBytesResult zeroProgress = IO::readAllBytes(zeroProgressReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectEq("readAllBytes rejects zero-progress reader", ErrorCode::ReadFailed, zeroProgress.status.code));

        ZeroProgressKnownSizeReader zeroProgressKnownSizeReader;
        const IO::Types::ReadAllBytesResult zeroProgressKnownSize = IO::readAllBytes(zeroProgressKnownSizeReader, IO::kNoByteLimit, 2);
        static_cast<void>(
            context.expectEq("readAllBytes rejects zero-progress known-size reader", ErrorCode::ReadFailed, zeroProgressKnownSize.status.code));

        IO::MemoryReader invalidBufferReader(spanOf(source));
        const IO::Types::ReadAllBytesResult invalidBuffer = IO::readAllBytes(invalidBufferReader, IO::kNoByteLimit, 0);
        static_cast<void>(context.expectEq("readAllBytes rejects zero buffer size", ErrorCode::InvalidArgument, invalidBuffer.status.code));
    }

    void testReadAllText(TestSupport::Context &context)
    {
        const std::string text("alpha\0beta", 10);

        IO::MemoryReader reader(bytesOf(text));
        const IO::Types::ReadAllTextResult result = IO::readAllText(reader, IO::kNoByteLimit, 3);
        static_cast<void>(context.expectTrue("readAllText succeeds", result.status.ok()));
        static_cast<void>(context.expectEq("readAllText preserves UTF-8 bytes and NUL", text, result.text));

        IO::MemoryReader exactLimitReader(bytesOf(text));
        const IO::Types::ReadAllTextResult exactLimit = IO::readAllText(exactLimitReader, text.size(), 4);
        static_cast<void>(context.expectTrue("readAllText accepts exact byte limit", exactLimit.status.ok()));
        static_cast<void>(context.expectEq("readAllText exact limit preserves text", text, exactLimit.text));

        IO::MemoryReader seekedReader(bytesOf(text));
        static_cast<void>(seekedReader.seek(5, IO::Types::SeekOrigin::Begin));
        const IO::Types::ReadAllTextResult seekedResult = IO::readAllText(seekedReader, 5, 2);
        static_cast<void>(context.expectTrue("readAllText known remaining after seek succeeds", seekedResult.status.ok()));
        static_cast<void>(context.expectEq("readAllText known remaining after seek preserves text", std::string("\0beta", 5), seekedResult.text));

        IO::MemoryReader shortLimitReader(bytesOf(text));
        const IO::Types::ReadAllTextResult shortLimit = IO::readAllText(shortLimitReader, text.size() - 1, 4);
        static_cast<void>(context.expectEq("readAllText rejects known size beyond limit", ErrorCode::SizeLimitExceeded, shortLimit.status.code));
        static_cast<void>(context.expectTrue("readAllText limit failure returns empty text", shortLimit.text.empty()));

        IO::MemoryReader zeroLimitReader(bytesOf(text));
        const IO::Types::ReadAllTextResult zeroLimit = IO::readAllText(zeroLimitReader, 0, 4);
        static_cast<void>(
            context.expectEq("readAllText zero limit rejects non-empty known stream", ErrorCode::SizeLimitExceeded, zeroLimit.status.code));
        static_cast<void>(context.expectTrue("readAllText zero limit returns empty text", zeroLimit.text.empty()));

        IO::MemoryReader emptyReader(bytesOf(""));
        const IO::Types::ReadAllTextResult emptyResult = IO::readAllText(emptyReader, IO::kNoByteLimit, 4);
        static_cast<void>(context.expectTrue("readAllText empty known size succeeds", emptyResult.status.ok()));
        static_cast<void>(context.expectTrue("readAllText empty known size returns empty text", emptyResult.text.empty()));

        std::array<std::byte, 2> scratch{};
        UnknownSizeChunkReader scratchReader(bytesOf(text), 1);
        const IO::Types::ReadAllTextResult scratchResult = IO::readAllText(scratchReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectTrue("readAllText scratch overload succeeds", scratchResult.status.ok()));
        static_cast<void>(context.expectEq("readAllText scratch overload preserves text", text, scratchResult.text));

        UnknownSizeChunkReader exactScratchLimitReader(bytesOf(text), 2, false);
        const IO::Types::ReadAllTextResult exactScratchLimit = IO::readAllText(exactScratchLimitReader, std::span<std::byte>(scratch), text.size());
        static_cast<void>(context.expectTrue("readAllText accepts exact unknown-size limit after EOF probe", exactScratchLimit.status.ok()));
        static_cast<void>(context.expectEq("readAllText exact unknown-size limit preserves text", text, exactScratchLimit.text));

        ProbeFailureReader probeFailureReader(bytesOf(text));
        const IO::Types::ReadAllTextResult probeFailure = IO::readAllText(probeFailureReader, std::span<std::byte>(scratch), text.size());
        static_cast<void>(context.expectEq("readAllText propagates limit probe failure", ErrorCode::PermissionDenied, probeFailure.status.code));
        static_cast<void>(context.expectEq("readAllText limit probe failure preserves limited text", text, probeFailure.text));

        UnknownSizeChunkReader scratchLimitReader(bytesOf(text), 2);
        const IO::Types::ReadAllTextResult scratchLimit = IO::readAllText(scratchLimitReader, std::span<std::byte>(scratch), 3);
        static_cast<void>(
            context.expectEq("readAllText rejects unknown-size stream beyond limit", ErrorCode::SizeLimitExceeded, scratchLimit.status.code));
        static_cast<void>(context.expectEq("readAllText limit failure preserves limited text", std::string{"alp"}, scratchLimit.text));

        const std::vector<std::byte> emptySource;
        UnknownSizeChunkReader emptyZeroLimitUnknownReader(spanOf(emptySource), 1);
        const IO::Types::ReadAllTextResult emptyZeroLimitUnknown = IO::readAllText(emptyZeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
        static_cast<void>(context.expectTrue("readAllText zero limit accepts empty unknown stream", emptyZeroLimitUnknown.status.ok()));

        UnknownSizeChunkReader zeroLimitUnknownReader(bytesOf(text), 1);
        const IO::Types::ReadAllTextResult zeroLimitUnknown = IO::readAllText(zeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
        static_cast<void>(
            context.expectEq("readAllText zero limit rejects non-empty unknown stream", ErrorCode::SizeLimitExceeded, zeroLimitUnknown.status.code));

        ReportedSizeReader shortKnownReader(bytesOf(text), text.size() + 2);
        const IO::Types::ReadAllTextResult shortKnown = IO::readAllText(shortKnownReader, IO::kNoByteLimit, 4);
        static_cast<void>(
            context.expectEq("readAllText reports early known-size EOF as PartialRead", ErrorCode::PartialRead, shortKnown.status.code));
        static_cast<void>(context.expectEq("readAllText partial read preserves text", text, shortKnown.text));

        FailingKnownSizeReader failingReader(bytesOf(text), 2, ErrorCode::PermissionDenied);
        const IO::Types::ReadAllTextResult failingResult = IO::readAllText(failingReader, IO::kNoByteLimit, 4);
        static_cast<void>(context.expectEq("readAllText direct fill propagates failure", ErrorCode::PermissionDenied, failingResult.status.code));
        static_cast<void>(context.expectEq("readAllText direct fill keeps text before failure", std::string{"alp"}, failingResult.text));

        SizeQueryFailureReader queryFailureReader;
        const IO::Types::ReadAllTextResult queryFailure = IO::readAllText(queryFailureReader, IO::kNoByteLimit, 4);
        static_cast<void>(context.expectEq("readAllText propagates size query failure", ErrorCode::PermissionDenied, queryFailure.status.code));
        static_cast<void>(context.expectFalse("readAllText does not read after size query failure", queryFailureReader.readCalled()));

        PositionQueryFailureReader positionQueryFailureReader;
        const IO::Types::ReadAllTextResult positionQueryFailure = IO::readAllText(positionQueryFailureReader, IO::kNoByteLimit, 4);
        static_cast<void>(
            context.expectEq("readAllText propagates position query failure", ErrorCode::PermissionDenied, positionQueryFailure.status.code));
        static_cast<void>(context.expectFalse("readAllText does not read after position query failure", positionQueryFailureReader.readCalled()));

        ImpossiblePositionReader impossiblePositionReader;
        const IO::Types::ReadAllTextResult impossiblePosition = IO::readAllText(impossiblePositionReader, IO::kNoByteLimit, 4);
        static_cast<void>(context.expectEq("readAllText rejects position beyond size", ErrorCode::InvalidArgument, impossiblePosition.status.code));
        static_cast<void>(context.expectFalse("readAllText does not read after impossible position", impossiblePositionReader.readCalled()));

        UnsupportedSizeReader unsupportedSizeReader(bytesOf(text));
        const IO::Types::ReadAllTextResult unsupportedSize = IO::readAllText(unsupportedSizeReader, IO::kNoByteLimit, 4);
        static_cast<void>(context.expectTrue("readAllText falls back when size is Unsupported", unsupportedSize.status.ok()));
        static_cast<void>(context.expectEq("readAllText Unsupported fallback preserves text", text, unsupportedSize.text));

        InvalidReadCountReader invalidCountReader;
        const IO::Types::ReadAllTextResult invalidCount = IO::readAllText(invalidCountReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectEq("readAllText rejects invalid reader byte count", ErrorCode::ReadFailed, invalidCount.status.code));

        UnknownSizeChunkReader zeroProgressReader(bytesOf(text), 0);
        const IO::Types::ReadAllTextResult zeroProgress = IO::readAllText(zeroProgressReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectEq("readAllText rejects zero-progress reader", ErrorCode::ReadFailed, zeroProgress.status.code));

        ZeroProgressKnownSizeReader zeroProgressKnownSizeReader;
        const IO::Types::ReadAllTextResult zeroProgressKnownSize = IO::readAllText(zeroProgressKnownSizeReader, IO::kNoByteLimit, 4);
        static_cast<void>(
            context.expectEq("readAllText rejects zero-progress known-size reader", ErrorCode::ReadFailed, zeroProgressKnownSize.status.code));

        UnknownSizeChunkReader emptyScratchReader(bytesOf(text), 1);
        const IO::Types::ReadAllTextResult emptyScratch = IO::readAllText(emptyScratchReader, std::span<std::byte>{});
        static_cast<void>(context.expectEq("readAllText rejects empty scratch", ErrorCode::InvalidArgument, emptyScratch.status.code));

        IO::MemoryReader invalidBufferReader(bytesOf(text));
        const IO::Types::ReadAllTextResult invalidBuffer = IO::readAllText(invalidBufferReader, IO::kNoByteLimit, 0);
        static_cast<void>(context.expectEq("readAllText rejects zero buffer size", ErrorCode::InvalidArgument, invalidBuffer.status.code));
    }

    void testWriteAllBytes(TestSupport::Context &context)
    {
        const std::vector<std::byte> source = makeBytes({0x01, 0x02, 0x00, 0x03, 0xff});

        ChunkedWriter chunkedWriter(2);
        const IO::Types::Status chunkedStatus = IO::writeAllBytes(chunkedWriter, source);
        static_cast<void>(context.expectTrue("writeAllBytes accepts partial successful writes", chunkedStatus.ok()));
        static_cast<void>(context.expectEq("writeAllBytes preserves bytes across partial writes", source, chunkedWriter.bytes()));

        ChunkedWriter zeroProgressWriter(0);
        const IO::Types::Status zeroProgressStatus = IO::writeAllBytes(zeroProgressWriter, spanOf(source));
        static_cast<void>(context.expectEq("writeAllBytes rejects zero-byte progress", ErrorCode::WriteFailed, zeroProgressStatus.code));

        FailingWriter failingWriter(ErrorCode::PermissionDenied);
        const IO::Types::Status failureStatus = IO::writeAllBytes(failingWriter, spanOf(source));
        static_cast<void>(context.expectEq("writeAllBytes propagates writer failure", ErrorCode::PermissionDenied, failureStatus.code));

        InvalidWriteCountWriter invalidCountWriter;
        const IO::Types::Status invalidCountStatus = IO::writeAllBytes(invalidCountWriter, spanOf(source));
        static_cast<void>(context.expectEq("writeAllBytes rejects invalid writer byte count", ErrorCode::WriteFailed, invalidCountStatus.code));

        FailingWriter emptyFailingWriter(ErrorCode::WriteFailed);
        const IO::Types::Status emptyStatus = IO::writeAllBytes(emptyFailingWriter, std::span<const std::byte>{});
        static_cast<void>(context.expectTrue("writeAllBytes empty input succeeds without calling writer", emptyStatus.ok()));
    }

    void testWriteAllText(TestSupport::Context &context)
    {
        const std::string text("a\0b\0c", 5);

        ChunkedWriter chunkedWriter(2);
        const IO::Types::Status chunkedStatus = IO::writeAllText(chunkedWriter, text);
        static_cast<void>(context.expectTrue("writeAllText accepts partial successful writes", chunkedStatus.ok()));
        static_cast<void>(context.expectEq("writeAllText preserves text bytes", copyBytes(bytesOf(text)), chunkedWriter.bytes()));

        FailingWriter failingWriter(ErrorCode::WriteFailed);
        const IO::Types::Status failureStatus = IO::writeAllText(failingWriter, text);
        static_cast<void>(context.expectEq("writeAllText propagates writer failure", ErrorCode::WriteFailed, failureStatus.code));
    }
} // namespace

namespace GameWIP::Test
{
    int runIOTests([[maybe_unused]] int argc, [[maybe_unused]] char **argv, const IOTestOptions &options)
    {
        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("IO test options: report={}", options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

        runner.runSuite("IO error code names", testErrorCodeNames);
        runner.runSuite("IO status defaults", testStatusAndDefaultContracts);
        runner.runSuite("IO MemoryReader", testMemoryReader);
        runner.runSuite("IO MemoryReader seek", testMemoryReaderSeek);
        runner.runSuite("IO MemoryWriter", testMemoryWriter);
        runner.runSuite("IO readAllBytes", testReadAllBytes);
        runner.runSuite("IO readAllText", testReadAllText);
        runner.runSuite("IO writeAllBytes", testWriteAllBytes);
        runner.runSuite("IO writeAllText", testWriteAllText);

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("IO library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
