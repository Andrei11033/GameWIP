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

    /// @brief Converts byte-value fixtures into character-backed storage without claiming text validity.
    [[nodiscard]] std::string makeStringBytes(std::initializer_list<unsigned int> values)
    {
        std::string bytes;
        bytes.reserve(values.size());

        for (const unsigned int value : values)
        {
            bytes.push_back(static_cast<char>(value));
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
    static_assert(std::is_same_v<decltype(std::declval<const IO::MemoryWriter &>().copyText()), IO::Types::CopyTextResult>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::string &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::string &&>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &&>);
    static_assert(std::is_same_v<
                  decltype(IO::writeAllBytes(std::declval<IO::Writer &>(), std::declval<std::span<const std::byte>>())),
                  IO::Types::WriteResult>);
    static_assert(std::is_same_v<decltype(IO::writeAllText(std::declval<IO::Writer &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);

#if IO_INTERNAL_TEST_HOOKS
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
        DeferredTextStorageFailureReader(std::span<const std::byte> bytes, IO::TestHooks::FailureKind failureKind)
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
                IO::TestHooks::forceNextFailure(IO::TestHooks::FailurePoint::ReadAllTextStorage, failureKind_);
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

#endif

    // Focused suite declarations keep cross-suite calls independent of fragment include order.
    void testErrorCodeNames(TestSupport::Context &context);
    void testStatusAndDefaultContracts(TestSupport::Context &context);
    void testMemoryReader(TestSupport::Context &context);
    void testMemoryReaderSeek(TestSupport::Context &context);
    void testMemoryWriter(TestSupport::Context &context);
#if IO_INTERNAL_TEST_HOOKS
    void testCheckedFailureTranslation(TestSupport::Context &context);
#endif
    void testReadAllBytes(TestSupport::Context &context);
    void testReadAllText(TestSupport::Context &context);
    void testWriteAllBytes(TestSupport::Context &context);
    void testWriteAllText(TestSupport::Context &context);

#include "validation/tests/io/contracts_test.inl"
#include "validation/tests/io/memory_test.inl"
#include "validation/tests/io/read_test.inl"
#include "validation/tests/io/write_test.inl"
} // namespace

namespace GameWIP::Test
{
    int runIOTests([[maybe_unused]] int argc, [[maybe_unused]] char **argv, const IOTestOptions &options)
    {
        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
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
#if IO_INTERNAL_TEST_HOOKS
        runner.runSuite("IO checked failure translation", testCheckedFailureTranslation);
#endif
        runner.runSuite("IO readAllBytes", testReadAllBytes);
        runner.runSuite("IO readAllText", testReadAllText);
        runner.runSuite("IO writeAllBytes", testWriteAllBytes);
        runner.runSuite("IO writeAllText", testWriteAllText);

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("IO library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
