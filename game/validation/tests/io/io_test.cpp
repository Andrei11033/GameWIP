/// @file io_test.cpp
/// @brief Executable self-tests for the IO library.
///
/// The suite exercises public transfer contracts, partial progress, limits,
/// seeking, error propagation, and custom Reader/Writer implementations.

#include "validation/tests/io/io_test.h"

#include "io/io.h"
#include "io/internal/io_test_hooks.h"
#include "test_support/test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <limits>
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

    /// @brief Converts integer literals into a byte vector for readable fixtures.
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

    /// @brief Copies a byte view into owning storage for result comparisons.
    [[nodiscard]] std::vector<std::byte> copyBytes(std::span<const std::byte> bytes)
    {
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    /// @brief Exposes vector bytes as the immutable span accepted by IO APIs.
    [[nodiscard]] std::span<const std::byte> spanOf(const std::vector<std::byte> &bytes) noexcept
    {
        return std::span<const std::byte>(bytes.data(), bytes.size());
    }

    /// @brief Exposes string bytes without copying or performing encoding conversion.
    [[nodiscard]] std::span<const std::byte> bytesOf(std::string_view text)
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    /// @brief Reader with only the required operation, used to verify optional-operation defaults.
    class MinimalReader final : public IO::Reader
    {
    public:
        /// @brief Returns a successful empty read while leaving optional operations inherited.
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
        {
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }
    };

    /// @brief Unknown-size reader that limits each transfer and can report EOF with the final bytes.
    class UnknownSizeChunkReader final : public IO::Reader
    {
    public:
        UnknownSizeChunkReader(std::span<const std::byte> bytes, std::size_t maxChunkSize, bool reportEndWithLastRead = true)
            : bytes_(bytes)
            , maxChunkSize_(maxChunkSize)
            , reportEndWithLastRead_(reportEndWithLastRead)
        {
        }

        /// @brief Copies at most maxChunkSize_ bytes and applies configured final-read EOF reporting.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
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

    /// @brief Known-size reader whose position query is unsupported.
    class SizeWithoutPositionReader final : public IO::Reader
    {
    public:
        SizeWithoutPositionReader(std::span<const std::byte> bytes, std::size_t initialPosition)
            : bytes_(bytes)
            , position_(initialPosition)
        {
        }

        /// @brief Reads remaining backing bytes from the internal position.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
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

        /// @brief Reports the complete backing size while position() remains inherited/unsupported.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = {}, .sizeBytes = static_cast<std::uint64_t>(bytes_.size())};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ = 0;
    };

    /// @brief Reader whose reported size can differ from its backing data size.
    class ReportedSizeReader final : public IO::Reader
    {
    public:
        ReportedSizeReader(std::span<const std::byte> bytes, std::uint64_t reportedSize)
            : bytes_(bytes)
            , reportedSize_(reportedSize)
        {
        }

        /// @brief Reads available backing bytes independently from the configured reported size.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
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

        /// @brief Reports the current backing-data read position.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override
        {
            return {.status = {}, .position = static_cast<std::uint64_t>(position_)};
        }

        /// @brief Returns the deliberately configurable logical size.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = {}, .sizeBytes = reportedSize_};
        }

    private:
        std::span<const std::byte> bytes_;
        std::uint64_t reportedSize_ = 0;
        std::size_t position_ = 0;
    };

    /// @brief Reader that fails its size query before any payload read.
    class SizeQueryFailureReader final : public IO::Reader
    {
    public:
        /// @brief Records an unexpected payload read after the size-query failure.
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
        {
            readCalled_ = true;
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        /// @brief Returns the configured size-query failure.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied), .sizeBytes = 0};
        }

        /// @brief Returns whether a whole-stream helper incorrectly attempted payload reading.
        [[nodiscard]] bool readCalled() const noexcept
        {
            return readCalled_;
        }

    private:
        bool readCalled_ = false;
    };

    /// @brief Reader that reports size but fails its current-position query.
    class PositionQueryFailureReader final : public IO::Reader
    {
    public:
        /// @brief Records an unexpected payload read after the position-query failure.
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
        {
            readCalled_ = true;
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        /// @brief Returns the configured position-query failure.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied), .position = 0};
        }

        /// @brief Reports a known logical size so position is queried next.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = {}, .sizeBytes = 1};
        }

        /// @brief Returns whether a whole-stream helper incorrectly attempted payload reading.
        [[nodiscard]] bool readCalled() const noexcept
        {
            return readCalled_;
        }

    private:
        bool readCalled_ = false;
    };

    /// @brief Reader that reports a position beyond its size to exercise invariant validation.
    class ImpossiblePositionReader final : public IO::Reader
    {
    public:
        /// @brief Records an unexpected payload read after impossible metadata is detected.
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
        {
            readCalled_ = true;
            return {.status = {}, .bytesRead = 0, .endOfStream = true};
        }

        /// @brief Reports a position greater than size().
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override
        {
            return {.status = {}, .position = 2};
        }

        /// @brief Reports the smaller logical size used by the invariant test.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = {}, .sizeBytes = 1};
        }

        /// @brief Returns whether a whole-stream helper incorrectly attempted payload reading.
        [[nodiscard]] bool readCalled() const noexcept
        {
            return readCalled_;
        }

    private:
        bool readCalled_ = false;
    };

    /// @brief Known-size reader that returns a successful zero-byte transfer without EOF.
    class ZeroProgressKnownSizeReader final : public IO::Reader
    {
    public:
        /// @brief Returns success with zero bytes and no EOF to simulate a stalled backend.
        [[nodiscard]] IO::Types::ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
        {
            return {.status = {}, .bytesRead = 0, .endOfStream = false};
        }

        /// @brief Reports the beginning of the logical stream.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override
        {
            return {.status = {}, .position = 0};
        }

        /// @brief Reports one remaining byte so zero progress is invalid.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = {}, .sizeBytes = 1};
        }
    };

    /// @brief Unknown-size reader backed by fixed bytes and normal EOF behavior.
    class UnsupportedSizeReader final : public IO::Reader
    {
    public:
        /// @brief Stores a non-owning fixture byte view for unknown-size draining.
        explicit UnsupportedSizeReader(std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        /// @brief Reads all available backing bytes and reports EOF when exhausted.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
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

        /// @brief Explicitly reports Unsupported so helpers take the unknown-size path.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = IO::makeStatus(ErrorCode::Unsupported), .sizeBytes = 0};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ = 0;
    };

    /// @brief Reader that fails the one-byte limit probe after returning its initial data.
    class ProbeFailureReader final : public IO::Reader
    {
    public:
        /// @brief Stores bytes returned before the hard-limit probe fails.
        explicit ProbeFailureReader(std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        /// @brief Returns backing bytes, then fails the next probe read.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
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

    /// @brief Broken reader that reports more bytes than the destination can hold.
    class InvalidReadCountReader final : public IO::Reader
    {
    public:
        /// @brief Deliberately reports one byte beyond destination capacity.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
        {
            return {.status = {}, .bytesRead = destination.size() + 1, .endOfStream = false};
        }
    };

    /// @brief Known-size reader that returns partial data before a configured failure.
    class FailingKnownSizeReader final : public IO::Reader
    {
    public:
        FailingKnownSizeReader(std::span<const std::byte> bytes, std::size_t failAfterBytes, ErrorCode failureCode)
            : bytes_(bytes)
            , failAfterBytes_(failAfterBytes)
            , failureCode_(failureCode)
        {
        }

        /// @brief Returns configured prefix progress, then the configured read failure.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
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
                return {.status = IO::makeStatus(failureCode_), .bytesRead = count, .endOfStream = false};
            }

            const std::size_t remainingBeforeFailure = failAfterBytes_ - position_;
            const std::size_t count = std::min(destination.size(), remainingBeforeFailure);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
        }

        /// @brief Reports current progress through the configured backing bytes.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override
        {
            return {.status = {}, .position = static_cast<std::uint64_t>(position_)};
        }

        /// @brief Reports the full backing size to select the known-size helper path.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override
        {
            return {.status = {}, .sizeBytes = static_cast<std::uint64_t>(bytes_.size())};
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t failAfterBytes_ = 0;
        ErrorCode failureCode_ = ErrorCode::ReadFailed;
        std::size_t position_ = 0;
    };

    /// @brief Writer that accepts bounded chunks and retains all accepted bytes.
    class ChunkedWriter final : public IO::Writer
    {
    public:
        /// @brief Sets the maximum bytes accepted by each write call.
        explicit ChunkedWriter(std::size_t maxChunkSize)
            : maxChunkSize_(maxChunkSize)
        {
        }

        /// @brief Accepts one bounded chunk and appends it to retained fixture bytes.
        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) noexcept override
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

        /// @brief Returns all bytes accepted across write calls.
        [[nodiscard]] const std::vector<std::byte> &bytes() const noexcept
        {
            return bytes_;
        }

    private:
        std::size_t maxChunkSize_ = 0;
        std::vector<std::byte> bytes_;
    };

    /// @brief Writer that fails every transfer without accepting bytes.
    class FailingWriter final : public IO::Writer
    {
    public:
        /// @brief Stores the portable error returned by every write.
        explicit FailingWriter(ErrorCode code)
            : code_(code)
        {
        }

        /// @brief Rejects every transfer with no accepted progress.
        [[nodiscard]] IO::Types::WriteResult write([[maybe_unused]] std::span<const std::byte> bytes) noexcept override
        {
            return {.status = IO::makeStatus(code_), .bytesWritten = 0};
        }

    private:
        ErrorCode code_ = ErrorCode::WriteFailed;
    };

    /// @brief Writer that accepts a prefix and reports failure in the same transfer.
    class PartialFailingWriter final : public IO::Writer
    {
    public:
        PartialFailingWriter(std::size_t acceptedBytes, ErrorCode code)
            : acceptedBytes_(acceptedBytes)
            , code_(code)
        {
        }

        /// @brief Accepts a configured prefix and returns failure in the same transfer.
        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) noexcept override
        {
            const std::size_t count = std::min(acceptedBytes_, bytes.size());
            return {.status = IO::makeStatus(code_), .bytesWritten = count};
        }

    private:
        std::size_t acceptedBytes_ = 0;
        ErrorCode code_ = ErrorCode::WriteFailed;
    };

    /// @brief Broken writer that reports accepting more bytes than it received.
    class InvalidWriteCountWriter final : public IO::Writer
    {
    public:
        /// @brief Deliberately reports one accepted byte beyond the input span.
        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) noexcept override
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
    static_assert(IO::isValidFlushMode(IO::Types::FlushMode::None));
    static_assert(IO::isValidFlushMode(IO::Types::FlushMode::Data));
    static_assert(IO::isValidFlushMode(IO::Types::FlushMode::DataAndMetadataBestEffort));
    static_assert(!IO::isValidFlushMode(static_cast<IO::Types::FlushMode>(-1)));
    static_assert(noexcept(IO::isValidFlushMode(IO::Types::FlushMode::None)));
    static_assert(noexcept(IO::makeStatus(ErrorCode::Unknown)));
    static_assert(noexcept(std::declval<IO::Reader &>().isOpen()));
    static_assert(noexcept(std::declval<IO::Reader &>().canSeek()));
    static_assert(noexcept(std::declval<IO::Reader &>().read(std::declval<std::span<std::byte>>())));
    static_assert(noexcept(std::declval<IO::Reader &>().close()));
    static_assert(noexcept(std::declval<IO::Reader &>().position()));
    static_assert(noexcept(std::declval<IO::Reader &>().size()));
    static_assert(noexcept(std::declval<IO::Reader &>().seek(0, IO::Types::SeekOrigin::Begin)));
    static_assert(noexcept(std::declval<IO::Writer &>().isOpen()));
    static_assert(noexcept(std::declval<IO::Writer &>().canSeek()));
    static_assert(noexcept(std::declval<IO::Writer &>().write(std::declval<std::span<const std::byte>>())));
    static_assert(noexcept(std::declval<IO::Writer &>().flush()));
    static_assert(noexcept(std::declval<IO::Writer &>().close()));
    static_assert(noexcept(std::declval<IO::Writer &>().position()));
    static_assert(noexcept(std::declval<IO::Writer &>().seek(0, IO::Types::SeekOrigin::Begin)));
    static_assert(noexcept(IO::readAllBytes(std::declval<IO::Reader &>())));
    static_assert(noexcept(IO::readAllText(std::declval<IO::Reader &>())));
    static_assert(noexcept(IO::writeAllBytes(std::declval<IO::Writer &>(), std::declval<std::span<const std::byte>>())));
    static_assert(noexcept(IO::writeAllText(std::declval<IO::Writer &>(), std::declval<std::string_view>())));
    static_assert(noexcept(std::declval<IO::MemoryWriter &>().reserve(0)));
    static_assert(noexcept(std::declval<const IO::MemoryWriter &>().copyText()));
    static_assert(std::is_constructible_v<IO::MemoryReader, std::string &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::string &&>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &&>);
    static_assert(std::is_same_v<
                  decltype(IO::writeAllBytes(std::declval<IO::Writer &>(), std::declval<std::span<const std::byte>>())),
                  IO::Types::WriteResult>);
    static_assert(std::is_same_v<decltype(IO::writeAllText(std::declval<IO::Writer &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);

    /// @brief Verifies every portable error-code name and the unknown-value fallback.
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
            ErrorCodeName{ErrorCode::MoveFailed, "MoveFailed"},
            ErrorCodeName{ErrorCode::ResizeFailed, "ResizeFailed"},
            ErrorCodeName{ErrorCode::LockFailed, "LockFailed"},
            ErrorCodeName{ErrorCode::UnlockFailed, "UnlockFailed"},
            ErrorCodeName{ErrorCode::DirectoryCreateFailed, "DirectoryCreateFailed"},
            ErrorCodeName{ErrorCode::DirectoryListFailed, "DirectoryListFailed"},
            ErrorCodeName{ErrorCode::DirectoryNotEmpty, "DirectoryNotEmpty"},
            ErrorCodeName{ErrorCode::PartialRead, "PartialRead"},
            ErrorCodeName{ErrorCode::PartialWrite, "PartialWrite"},
            ErrorCodeName{ErrorCode::SizeLimitExceeded, "SizeLimitExceeded"},
            ErrorCodeName{ErrorCode::OutOfMemory, "OutOfMemory"},
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

    /// @brief Verifies status helpers and default Reader/Writer optional-operation contracts.
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
        static_cast<void>(context.expectEq(
            "Writer rejects invalid flush modes",
            ErrorCode::InvalidArgument,
            writer.flush(static_cast<IO::Types::FlushMode>(-1)).code));
        static_cast<void>(context.expectTrue("Writer close default succeeds", writer.close().ok()));
        static_cast<void>(context.expectTrue("Stateless Writer remains open after default close", writer.isOpen()));
        static_cast<void>(context.expectEq("Writer position defaults to NotSeekable", ErrorCode::NotSeekable, writer.position().status.code));
        static_cast<void>(
            context.expectEq("Writer seek defaults to NotSeekable", ErrorCode::NotSeekable, writer.seek(0, IO::Types::SeekOrigin::Begin).code));
    }

    /// @brief Verifies MemoryReader construction, reads, state, and close behavior.
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

    /// @brief Verifies MemoryReader seek origins, bounds checks, and position updates.
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

    /// @brief Verifies MemoryWriter ownership, aliasing, reserve, clear, and close behavior.
    void testMemoryWriter(TestSupport::Context &context)
    {
        IO::MemoryWriter writer;
        const std::vector<std::byte> source = makeBytes({0x01, 0x00, 0x02, 0xff});
        const IO::Types::Status initialReserve = writer.reserve(8);
        static_cast<void>(context.expectTrue("MemoryWriter initial reserve succeeds", initialReserve.ok()));
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
        static_cast<void>(context.expectEq(
            "MemoryWriter rejects invalid flush modes",
            ErrorCode::InvalidArgument,
            writer.flush(static_cast<IO::Types::FlushMode>(-1)).code));

        const IO::Types::Status largerReserve = writer.reserve(32);
        static_cast<void>(context.expectTrue("MemoryWriter larger reserve succeeds", largerReserve.ok()));
        static_cast<void>(context.expectTrue("MemoryWriter reserve grows capacity", writer.capacity() >= 32));

        IO::MemoryWriter textWriter;
        static_cast<void>(IO::writeAllText(textWriter, std::string_view{"a\0b", 3}));
        const IO::Types::TextCopyResult textCopy = textWriter.copyText();
        static_cast<void>(context.expectTrue("MemoryWriter text copy succeeds", textCopy.status.ok()));
        static_cast<void>(context.expectEq("MemoryWriter text copy preserves NUL bytes", std::string("a\0b", 3), textCopy.text));
        IO::MemoryWriter movedWriter(std::move(textWriter));
        const IO::Types::TextCopyResult movedText = movedWriter.copyText();
        static_cast<void>(context.expectTrue("MemoryWriter moved text copy succeeds", movedText.status.ok()));
        static_cast<void>(context.expectEq("MemoryWriter move preserves bytes", std::string("a\0b", 3), movedText.text));
        static_cast<void>(context.expectTrue("MemoryWriter move preserves open state", movedWriter.isOpen()));

        IO::MemoryWriter emptyTextWriter;
        const IO::Types::TextCopyResult emptyText = emptyTextWriter.copyText();
        static_cast<void>(context.expectTrue("MemoryWriter empty text copy succeeds", emptyText.status.ok()));
        static_cast<void>(context.expectTrue("MemoryWriter empty text copy is empty", emptyText.text.empty()));

        IO::MemoryWriter aliasWriter;
        static_cast<void>(aliasWriter.reserve(source.size()));
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
        static_cast<void>(context.expectTrue("MemoryWriter reserve remains available after close", writer.reserve(64).ok()));
        static_cast<void>(context.expectTrue("MemoryWriter copied text remains available after close", writer.copyText().status.ok()));
        static_cast<void>(context.expectTrue("MemoryWriter repeated close succeeds", writer.close().ok()));
    }

#if INTERNAL_IO_TEST_HOOKS
    /// @brief Resets process-wide IO failure injection before and after one validation scenario.
    class ScopedIOFailureHooks final
    {
    public:
        ScopedIOFailureHooks() noexcept
        {
            IO::TestHooks::reset();
        }

        ScopedIOFailureHooks(const ScopedIOFailureHooks &) = delete;
        ScopedIOFailureHooks &operator=(const ScopedIOFailureHooks &) = delete;

        ~ScopedIOFailureHooks() noexcept
        {
            IO::TestHooks::reset();
        }
    };

    /// @brief Unknown-size reader that arms a text-storage failure after one successful chunk.
    class DeferredTextStorageFailureReader final : public IO::Reader
    {
    public:
        DeferredTextStorageFailureReader(
            std::span<const std::byte> bytes,
            IO::TestHooks::FailureKind failureKind)
            : bytes_(bytes)
            , failureKind_(failureKind)
        {
        }

        /// @brief Returns one byte per read and arms the configured failure before the second append.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override
        {
            if (destination.empty())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = position_ >= bytes_.size()};
            }

            if (position_ >= bytes_.size())
            {
                return {.status = {}, .bytesRead = 0, .endOfStream = true};
            }

            if (readCount_ == 1)
            {
                IO::TestHooks::forceNextFailure(
                    IO::TestHooks::FailurePoint::ReadAllTextStorage,
                    failureKind_);
            }

            const std::size_t count = std::min<std::size_t>(1, bytes_.size() - position_);
            std::copy_n(bytes_.data() + position_, count, destination.data());
            position_ += count;
            ++readCount_;

            return {.status = {}, .bytesRead = count, .endOfStream = position_ == bytes_.size()};
        }

    private:
        std::span<const std::byte> bytes_;
        IO::TestHooks::FailureKind failureKind_ = IO::TestHooks::FailureKind::None;
        std::size_t position_ = 0;
        std::size_t readCount_ = 0;
    };

    /// @brief Verifies deterministic allocation, length, and unexpected-exception translation.
    void testCheckedFailureTranslation(TestSupport::Context &context)
    {
        using FailureKind = IO::TestHooks::FailureKind;
        using FailurePoint = IO::TestHooks::FailurePoint;

        const ScopedIOFailureHooks hooks;
        const std::vector<std::byte> source = makeBytes({0x41, 0x42, 0x43});

        IO::MemoryWriter writer;
        static_cast<void>(writer.write(source));
        const std::vector<std::byte> original = copyBytes(writer.bytes());

        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterReserve, FailureKind::OutOfMemory);
        static_cast<void>(context.expectEq("MemoryWriter reserve translates allocation failure", ErrorCode::OutOfMemory, writer.reserve(64).code));

        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterReserve, FailureKind::LengthError);
        static_cast<void>(context.expectEq("MemoryWriter reserve translates length failure", ErrorCode::SizeLimitExceeded, writer.reserve(64).code));

        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterReserve, FailureKind::Unexpected);
        static_cast<void>(context.expectEq("MemoryWriter reserve translates unexpected failure", ErrorCode::Unknown, writer.reserve(64).code));

        const IO::Types::Status oversizeReserve = writer.reserve(std::numeric_limits<std::size_t>::max());
        static_cast<void>(context.expectEq("MemoryWriter reserve rejects impossible capacity", ErrorCode::SizeLimitExceeded, oversizeReserve.code));

        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterCopyText, FailureKind::OutOfMemory);
        const IO::Types::TextCopyResult allocationText = writer.copyText();
        static_cast<void>(
            context.expectEq("MemoryWriter copyText translates allocation failure", ErrorCode::OutOfMemory, allocationText.status.code));
        static_cast<void>(context.expectTrue("MemoryWriter failed text copy returns no invalid text", allocationText.text.empty()));

        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterCopyText, FailureKind::LengthError);
        static_cast<void>(
            context.expectEq("MemoryWriter copyText translates length failure", ErrorCode::SizeLimitExceeded, writer.copyText().status.code));

        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterCopyText, FailureKind::Unexpected);
        static_cast<void>(context.expectEq("MemoryWriter copyText translates unexpected failure", ErrorCode::Unknown, writer.copyText().status.code));

        for (const auto [kind, expected] :
             {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
              std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
              std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
        {
            IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterWrite, kind);
            const IO::Types::WriteResult failedWrite = writer.write(source);
            static_cast<void>(context.expectEq("MemoryWriter write translates injected failure", expected, failedWrite.status.code));
            static_cast<void>(context.expectEq("MemoryWriter failed write reports zero progress", std::size_t{0}, failedWrite.bytesWritten));
            static_cast<void>(context.expectEq("MemoryWriter failed write preserves bytes", original, copyBytes(writer.bytes())));
        }

        for (const auto [kind, expected] :
             {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
              std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
              std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
        {
            IO::MemoryReader bytesReader(source);
            IO::TestHooks::forceNextFailure(FailurePoint::ReadAllBytesStorage, kind);
            const IO::Types::ReadAllBytesResult bytesResult = IO::readAllBytes(bytesReader);
            static_cast<void>(context.expectEq("readAllBytes translates output-storage failure", expected, bytesResult.status.code));
            static_cast<void>(context.expectTrue("readAllBytes storage failure returns no invalid bytes", bytesResult.bytes.empty()));

            IO::MemoryReader textReader(source);
            IO::TestHooks::forceNextFailure(FailurePoint::ReadAllTextStorage, kind);
            const IO::Types::ReadAllTextResult textResult = IO::readAllText(textReader);
            static_cast<void>(context.expectEq("readAllText translates output-storage failure", expected, textResult.status.code));
            static_cast<void>(context.expectTrue("readAllText storage failure returns no invalid text", textResult.text.empty()));
        }

        std::array<std::byte, 2> textScratch{};
        for (const auto [kind, expected] :
             {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
              std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
              std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
        {
            DeferredTextStorageFailureReader textAppendReader(spanOf(source), kind);
            const IO::Types::ReadAllTextResult textAppendFailure =
                IO::readAllText(textAppendReader, std::span<std::byte>(textScratch));
            static_cast<void>(context.expectEq(
                "readAllText translates unknown-size append failure",
                expected,
                textAppendFailure.status.code));
            static_cast<void>(context.expectEq(
                "readAllText unknown-size append failure preserves prior text",
                std::string{"A"},
                textAppendFailure.text));
        }

        UnknownSizeChunkReader scratchAllocationReader(spanOf(source), 1);
        IO::TestHooks::forceNextFailure(FailurePoint::ReadAllScratchAllocation, FailureKind::OutOfMemory);
        static_cast<void>(context.expectEq(
            "readAllBytes translates scratch allocation failure",
            ErrorCode::OutOfMemory,
            IO::readAllBytes(scratchAllocationReader).status.code));

        std::array<std::byte, 2> scratch{};
        UnknownSizeChunkReader appendReader(spanOf(source), 1);
        IO::TestHooks::forceNextFailure(FailurePoint::ReadAllBytesStorage, FailureKind::OutOfMemory);
        const IO::Types::ReadAllBytesResult appendFailure = IO::readAllBytes(appendReader, std::span<std::byte>(scratch));
        static_cast<void>(context.expectEq("readAllBytes translates append allocation failure", ErrorCode::OutOfMemory, appendFailure.status.code));
        static_cast<void>(context.expectTrue("readAllBytes append failure returns no invalid bytes", appendFailure.bytes.empty()));
    }
#endif

    /// @brief Verifies whole-reader byte draining across known-size, unknown-size, limit, and failure paths.
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

    /// @brief Verifies whole-reader text draining and byte-limit behavior.
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

    /// @brief Verifies complete byte writes, partial progress, zero progress, and invalid backend counts.
    void testWriteAllBytes(TestSupport::Context &context)
    {
        const std::vector<std::byte> source = makeBytes({0x01, 0x02, 0x00, 0x03, 0xff});

        ChunkedWriter chunkedWriter(2);
        const IO::Types::WriteResult chunkedResult = IO::writeAllBytes(chunkedWriter, source);
        static_cast<void>(context.expectTrue("writeAllBytes accepts partial successful writes", chunkedResult.status.ok()));
        static_cast<void>(context.expectEq("writeAllBytes reports complete progress", source.size(), chunkedResult.bytesWritten));
        static_cast<void>(context.expectEq("writeAllBytes preserves bytes across partial writes", source, chunkedWriter.bytes()));

        ChunkedWriter zeroProgressWriter(0);
        const IO::Types::WriteResult zeroProgressResult = IO::writeAllBytes(zeroProgressWriter, spanOf(source));
        static_cast<void>(context.expectEq("writeAllBytes rejects zero-byte progress", ErrorCode::WriteFailed, zeroProgressResult.status.code));
        static_cast<void>(context.expectEq("writeAllBytes zero progress reports zero", std::size_t{0}, zeroProgressResult.bytesWritten));

        FailingWriter failingWriter(ErrorCode::PermissionDenied);
        const IO::Types::WriteResult failureResult = IO::writeAllBytes(failingWriter, spanOf(source));
        static_cast<void>(context.expectEq("writeAllBytes propagates writer failure", ErrorCode::PermissionDenied, failureResult.status.code));
        static_cast<void>(context.expectEq("writeAllBytes immediate failure reports zero", std::size_t{0}, failureResult.bytesWritten));

        PartialFailingWriter partialFailingWriter(2, ErrorCode::PermissionDenied);
        const IO::Types::WriteResult partialFailureResult = IO::writeAllBytes(partialFailingWriter, spanOf(source));
        static_cast<void>(
            context.expectEq("writeAllBytes partial failure propagates status", ErrorCode::PermissionDenied, partialFailureResult.status.code));
        static_cast<void>(context.expectEq("writeAllBytes partial failure preserves progress", std::size_t{2}, partialFailureResult.bytesWritten));

        InvalidWriteCountWriter invalidCountWriter;
        const IO::Types::WriteResult invalidCountResult = IO::writeAllBytes(invalidCountWriter, spanOf(source));
        static_cast<void>(
            context.expectEq("writeAllBytes rejects invalid writer byte count", ErrorCode::WriteFailed, invalidCountResult.status.code));
        static_cast<void>(context.expectEq("writeAllBytes invalid count preserves prior progress", std::size_t{0}, invalidCountResult.bytesWritten));

        FailingWriter emptyFailingWriter(ErrorCode::WriteFailed);
        const IO::Types::WriteResult emptyResult = IO::writeAllBytes(emptyFailingWriter, std::span<const std::byte>{});
        static_cast<void>(context.expectTrue("writeAllBytes empty input succeeds without calling writer", emptyResult.status.ok()));
        static_cast<void>(context.expectEq("writeAllBytes empty input reports zero", std::size_t{0}, emptyResult.bytesWritten));
    }

    /// @brief Verifies text writes preserve byte progress and failure status.
    void testWriteAllText(TestSupport::Context &context)
    {
        const std::string text("a\0b\0c", 5);

        ChunkedWriter chunkedWriter(2);
        const IO::Types::WriteResult chunkedResult = IO::writeAllText(chunkedWriter, text);
        static_cast<void>(context.expectTrue("writeAllText accepts partial successful writes", chunkedResult.status.ok()));
        static_cast<void>(context.expectEq("writeAllText reports complete progress", text.size(), chunkedResult.bytesWritten));
        static_cast<void>(context.expectEq("writeAllText preserves text bytes", copyBytes(bytesOf(text)), chunkedWriter.bytes()));

        FailingWriter failingWriter(ErrorCode::WriteFailed);
        const IO::Types::WriteResult failureResult = IO::writeAllText(failingWriter, text);
        static_cast<void>(context.expectEq("writeAllText propagates writer failure", ErrorCode::WriteFailed, failureResult.status.code));
        static_cast<void>(context.expectEq("writeAllText immediate failure reports zero", std::size_t{0}, failureResult.bytesWritten));
    }
} // namespace

namespace GameWIP::Test
{
    int runIOTests([[maybe_unused]] int argc, [[maybe_unused]] char **argv, const IOTestOptions &options)
    {
        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
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
#if INTERNAL_IO_TEST_HOOKS
        runner.runSuite("IO checked failure translation", testCheckedFailureTranslation);
#endif
        runner.runSuite("IO readAllBytes", testReadAllBytes);
        runner.runSuite("IO readAllText", testReadAllText);
        runner.runSuite("IO writeAllBytes", testWriteAllBytes);
        runner.runSuite("IO writeAllText", testWriteAllText);

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("IO library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
