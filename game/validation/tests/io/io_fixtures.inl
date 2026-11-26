/// @file io_fixtures.inl
/// @brief Test-only Reader and Writer fixtures for IO correctness tests.

/// ------------------------------------------------------------
/// Fixture values
/// ------------------------------------------------------------

namespace StatusTypes = GameWIP::Status::Types;
using ErrorCode = StatusTypes::Code;
using ReadResult = StatusTypes::Result<IO::Types::ReadProgress>;
using ScalarResult = StatusTypes::Result<std::uint64_t>;
using WriteResult = StatusTypes::Result<IO::Types::WriteProgress>;

[[nodiscard]] ReadResult readSuccess(std::size_t bytesRead, bool endOfStream)
{
    return ReadResult::success({.bytesRead = bytesRead, .endOfStream = endOfStream});
}

[[nodiscard]] ReadResult readFailure(ErrorCode code, std::size_t bytesRead = 0, bool endOfStream = false)
{
    return ReadResult::failure(StatusTypes::Status(code), {.bytesRead = bytesRead, .endOfStream = endOfStream});
}

[[nodiscard]] ScalarResult scalarSuccess(std::uint64_t value)
{
    return ScalarResult::success(value);
}

[[nodiscard]] ScalarResult scalarFailure(ErrorCode code)
{
    return ScalarResult::failure(StatusTypes::Status(code));
}

[[nodiscard]] WriteResult writeSuccess(std::size_t bytesWritten)
{
    return WriteResult::success({.bytesWritten = bytesWritten});
}

[[nodiscard]] WriteResult writeFailure(ErrorCode code, std::size_t bytesWritten = 0)
{
    return WriteResult::failure(StatusTypes::Status(code), {.bytesWritten = bytesWritten});
}

/// @brief Converts integer literals into a byte vector for readable fixtures.
[[nodiscard]] std::vector<std::byte> makeBytes(std::initializer_list<unsigned int> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());

    for(const unsigned int value : values)
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

    for(const unsigned int value : values)
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

/// ------------------------------------------------------------
/// Reader fixtures
/// ------------------------------------------------------------

/// @brief Reader with only the required operation, used to verify optional-operation defaults.
class MinimalReader final : public IO::Reader
{
public:
    /// @brief Returns a successful empty read while leaving optional operations inherited.
    [[nodiscard]] ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
    {
        return readSuccess(0, true);
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        if(destination.empty())
        {
            return readSuccess(0, position_ >= bytes_.size());
        }

        if(position_ >= bytes_.size())
        {
            return readSuccess(0, true);
        }
        const std::size_t remaining = bytes_.size() - position_;
        const std::size_t count = std::min(destination.size(), std::min(maxChunkSize_, remaining));
        std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
        position_ += count;

        return readSuccess(count, reportEndWithLastRead_ && position_ == bytes_.size());
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        if(position_ >= bytes_.size())
        {
            return readSuccess(0, true);
        }
        const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
        std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
        position_ += count;
        return readSuccess(count, position_ == bytes_.size());
    }

    /// @brief Reports the complete backing size while position() remains inherited/unsupported.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarSuccess(static_cast<std::uint64_t>(bytes_.size()));
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        if(position_ >= bytes_.size())
        {
            return readSuccess(0, true);
        }
        const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
        std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
        position_ += count;
        return readSuccess(count, position_ == bytes_.size());
    }

    /// @brief Reports the current backing-data read position.
    [[nodiscard]] ScalarResult position() const noexcept override
    {
        return scalarSuccess(static_cast<std::uint64_t>(position_));
    }

    /// @brief Returns the deliberately configurable logical size.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarSuccess(reportedSize_);
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
    [[nodiscard]] ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
    {
        readCalled_ = true;
        return readSuccess(0, true);
    }

    /// @brief Returns the configured size-query failure.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarFailure(ErrorCode::PermissionDenied);
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
    [[nodiscard]] ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
    {
        readCalled_ = true;
        return readSuccess(0, true);
    }

    /// @brief Returns the configured position-query failure.
    [[nodiscard]] ScalarResult position() const noexcept override
    {
        return scalarFailure(ErrorCode::PermissionDenied);
    }

    /// @brief Reports a known logical size so position is queried next.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarSuccess(1);
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
    [[nodiscard]] ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
    {
        readCalled_ = true;
        return readSuccess(0, true);
    }

    /// @brief Reports a position greater than size().
    [[nodiscard]] ScalarResult position() const noexcept override
    {
        return scalarSuccess(2);
    }

    /// @brief Reports the smaller logical size used by the invariant test.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarSuccess(1);
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
    [[nodiscard]] ReadResult read([[maybe_unused]] std::span<std::byte> destination) noexcept override
    {
        return readSuccess(0, false);
    }

    /// @brief Reports the beginning of the logical stream.
    [[nodiscard]] ScalarResult position() const noexcept override
    {
        return scalarSuccess(0);
    }

    /// @brief Reports one remaining byte so zero progress is invalid.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarSuccess(1);
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        if(position_ >= bytes_.size())
        {
            return readSuccess(0, true);
        }
        const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
        std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
        position_ += count;
        return readSuccess(count, position_ == bytes_.size());
    }

    /// @brief Explicitly reports Unsupported so helpers take the unknown-size path.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarFailure(ErrorCode::Unsupported);
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        if(position_ >= bytes_.size())
        {
            return readFailure(ErrorCode::PermissionDenied);
        }
        const std::size_t count = std::min(destination.size(), bytes_.size() - position_);
        std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
        position_ += count;
        return readSuccess(count, false);
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        return readSuccess(destination.size() + 1, false);
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
    [[nodiscard]] ReadResult read(std::span<std::byte> destination) noexcept override
    {
        if(destination.empty())
        {
            return readSuccess(0, position_ >= bytes_.size());
        }

        if(position_ >= bytes_.size())
        {
            return readSuccess(0, true);
        }

        if(position_ >= failAfterBytes_)
        {
            const std::size_t count = std::min<std::size_t>(destination.size(), 1);
            std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
            position_ += count;
            return readFailure(failureCode_, count, false);
        }
        const std::size_t remainingBeforeFailure = failAfterBytes_ - position_;
        const std::size_t count = std::min(destination.size(), remainingBeforeFailure);
        std::ranges::copy(bytes_.subspan(position_, count), destination.begin());
        position_ += count;
        return readSuccess(count, position_ == bytes_.size());
    }

    /// @brief Reports current progress through the configured backing bytes.
    [[nodiscard]] ScalarResult position() const noexcept override
    {
        return scalarSuccess(static_cast<std::uint64_t>(position_));
    }

    /// @brief Reports the full backing size to select the known-size helper path.
    [[nodiscard]] ScalarResult size() const noexcept override
    {
        return scalarSuccess(static_cast<std::uint64_t>(bytes_.size()));
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t failAfterBytes_ = 0;
    ErrorCode failureCode_ = ErrorCode::ReadFailed;
    std::size_t position_ = 0;
};

/// ------------------------------------------------------------
/// Writer fixtures
/// ------------------------------------------------------------

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
    [[nodiscard]] WriteResult write(std::span<const std::byte> bytes) noexcept override
    {
        ++writeCalls_;

        if(bytes.empty())
        {
            return writeSuccess(0);
        }

        if(maxChunkSize_ == 0)
        {
            return writeSuccess(0);
        }
        const std::size_t count = std::min(maxChunkSize_, bytes.size());
        const std::span<const std::byte> accepted = bytes.first(count);
        bytes_.insert(bytes_.end(), accepted.begin(), accepted.end());
        return writeSuccess(count);
    }

    /// @brief Returns all bytes accepted across write calls.
    [[nodiscard]] const std::vector<std::byte> &bytes() const noexcept
    {
        return bytes_;
    }

    /// @brief Counts every write attempt, including empty requests.
    [[nodiscard]] std::size_t writeCalls() const noexcept
    {
        return writeCalls_;
    }

private:
    std::size_t maxChunkSize_ = 0;
    std::size_t writeCalls_ = 0;
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
    [[nodiscard]] WriteResult write([[maybe_unused]] std::span<const std::byte> bytes) noexcept override
    {
        return writeFailure(code_);
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
    [[nodiscard]] WriteResult write(std::span<const std::byte> bytes) noexcept override
    {
        const std::size_t count = std::min(acceptedBytes_, bytes.size());
        return writeFailure(code_, count);
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
    [[nodiscard]] WriteResult write(std::span<const std::byte> bytes) noexcept override
    {
        return writeSuccess(bytes.size() + 1);
    }
};
