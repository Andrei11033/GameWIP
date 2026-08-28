/// @file filesystem_handles.inl
/// @brief FileSystem cursor, file-handle, reader, writer, and lock operations.

DirectoryCursor::DirectoryCursor() noexcept = default;

DirectoryCursor::DirectoryCursor(DirectoryCursor &&other) noexcept = default;

DirectoryCursor &DirectoryCursor::operator=(DirectoryCursor &&other) noexcept = default;

DirectoryCursor::~DirectoryCursor() noexcept = default;

IO::Types::Status DirectoryCursor::open(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
{
    try
    {
        if (state_)
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }
        if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        Detail::Platform::DirectoryCursorOpenResult opened = Detail::Platform::openDirectoryCursor(path, options.symlinkPolicy);
        if (!opened.status.ok())
        {
            return std::move(opened.status);
        }

        options_ = options;
        emittedEntries_ = 0;
        limitReached_ = false;
        state_ = std::move(opened.state);
        return IO::successStatus();
    }
    catch (const std::bad_alloc &)
    {
        return IO::makeStatus(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return IO::makeStatus(ErrorCode::Unknown);
    }
}

bool DirectoryCursor::isOpen() const noexcept
{
    return state_ != nullptr;
}

Types::Directory::CursorNextResult DirectoryCursor::next() noexcept
{
    try
    {
        if (!state_)
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }
        if (limitReached_)
        {
            return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
        }

        while (true)
        {
            Detail::Platform::DirectoryCursorNextResult next = Detail::Platform::readDirectoryCursor(*state_);
            if (!next.status.ok())
            {
                return {.status = next.status};
            }
            if (!next.hasEntry)
            {
                return {.status = IO::successStatus()};
            }
            if ((!options_.includeHidden && next.hidden) || !includeDirectoryEntryKind(next.entry.info.kind, options_))
            {
                continue;
            }
            if (emittedEntries_ >= options_.maxEntries)
            {
                limitReached_ = true;
                return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
            }

            ++emittedEntries_;
            return {.status = IO::successStatus(), .entry = std::move(next.entry), .hasEntry = true};
        }
    }
    catch (const std::bad_alloc &)
    {
        return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
    }
    catch (...)
    {
        return {.status = IO::makeStatus(ErrorCode::Unknown)};
    }
}

IO::Types::Status DirectoryCursor::close() noexcept
{
    state_.reset();
    emittedEntries_ = 0;
    limitReached_ = false;
    return IO::successStatus();
}

FileLock::FileLock() noexcept = default;

FileLock::FileLock(FileLock &&other) noexcept
    : state_(std::move(other.state_))
    , mode_(other.mode_)
{
}

FileLock::~FileLock() noexcept
{
    static_cast<void>(unlock());
}

bool FileLock::active() const noexcept
{
    return state_ != nullptr && state_->active;
}

Types::Lock::Mode FileLock::mode() const noexcept
{
    return mode_;
}

IO::Types::Status FileLock::unlock() noexcept
{
    if (!active())
    {
        state_.reset();
        return IO::successStatus();
    }

    IO::Types::Status status = Detail::Platform::unlockFile(*state_);
    if (status.ok())
    {
        state_.reset();
    }
    return status;
}

FileLock::FileLock(std::unique_ptr<Detail::FileLockState> state, Types::Lock::Mode mode) noexcept
    : state_(std::move(state))
    , mode_(mode)
{
}

FileReader::FileReader() noexcept = default;

FileReader::FileReader(FileReader &&other) noexcept
    : state_(std::move(other.state_))
{
}

FileReader::~FileReader() noexcept
{
    static_cast<void>(close());
}

IO::Types::Status FileReader::open(const Types::Path &path, const Types::File::ReaderOpenOptions &options) noexcept
{
    if (isOpen())
    {
        return IO::makeStatus(ErrorCode::AlreadyOpen);
    }
    if (!isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
    {
        return IO::makeStatus(ErrorCode::InvalidArgument);
    }

    std::unique_ptr<Detail::FileState> newState;
    IO::Types::Status status = Detail::Platform::openReader(newState, path, options);
    if (status.ok())
    {
        state_ = std::move(newState);
    }
    return status;
}

bool FileReader::isOpen() const noexcept
{
    return stateIsOpen(state_);
}

bool FileReader::canSeek() const noexcept
{
    return isOpen();
}

IO::Types::ReadResult FileReader::read(std::span<std::byte> destination) noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    return Detail::Platform::readFile(*state_, destination);
}

IO::Types::Status FileReader::close() noexcept
{
    if (!isOpen())
    {
        state_.reset();
        return IO::successStatus();
    }

    IO::Types::Status status = Detail::Platform::closeFile(*state_);
    if (status.ok())
    {
        state_.reset();
    }
    return status;
}

IO::Types::PositionResult FileReader::position() const noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    return Detail::Platform::filePosition(*state_);
}

IO::Types::SizeResult FileReader::size() const noexcept
{
    if (!isOpen())
    {
        return sizeFailure(ErrorCode::NotOpen);
    }

    return Detail::Platform::fileSize(*state_);
}

IO::Types::Status FileReader::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
{
    if (!isOpen())
    {
        return IO::makeStatus(ErrorCode::NotOpen);
    }

    return Detail::Platform::seekFile(*state_, offset, origin);
}

Types::Lock::Result FileReader::tryLockShared() noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::Lock::Mode::Shared);
    if (!result.status.ok() || result.outcome == Types::Lock::Outcome::WouldBlock)
    {
        return {.status = std::move(result.status), .outcome = result.outcome};
    }

    return {
        .status = IO::successStatus(),
        .outcome = Types::Lock::Outcome::Acquired,
        .lock = FileLock(std::move(result.state), Types::Lock::Mode::Shared)};
}

FileWriter::FileWriter() noexcept = default;

FileWriter::FileWriter(FileWriter &&other) noexcept
    : state_(std::move(other.state_))
{
}

FileWriter::~FileWriter() noexcept
{
    static_cast<void>(close());
}

IO::Types::Status FileWriter::open(const Types::Path &path, const Types::File::WriterOpenOptions &options) noexcept
{
    if (isOpen())
    {
        return IO::makeStatus(ErrorCode::AlreadyOpen);
    }
    if (!isValidFileWriterMode(options.mode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
        !IO::isValidFlushMode(options.flushOnClose))
    {
        return IO::makeStatus(ErrorCode::InvalidArgument);
    }

    IO::Types::Status parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);
    if (!parentStatus.ok())
    {
        return parentStatus;
    }

    std::unique_ptr<Detail::FileState> newState;
    IO::Types::Status status = Detail::Platform::openWriter(newState, path, options);
    if (status.ok())
    {
        state_ = std::move(newState);
    }
    return status;
}

bool FileWriter::isOpen() const noexcept
{
    return stateIsOpen(state_);
}

bool FileWriter::canSeek() const noexcept
{
    return isOpen() && !state_->appendMode;
}

IO::Types::WriteResult FileWriter::write(std::span<const std::byte> bytes) noexcept
{
    if (!isOpen())
    {
        return writeFailure(ErrorCode::NotOpen);
    }

    return Detail::Platform::writeFile(*state_, bytes);
}

IO::Types::Status FileWriter::flush(IO::Types::FlushMode mode) noexcept
{
    if (!isOpen())
    {
        return IO::makeStatus(ErrorCode::NotOpen);
    }

    return Detail::Platform::flushFile(*state_, mode);
}

IO::Types::Status FileWriter::close() noexcept
{
    if (!isOpen())
    {
        state_.reset();
        return IO::successStatus();
    }

    IO::Types::Status status = Detail::Platform::closeFile(*state_);
    if (status.ok())
    {
        state_.reset();
    }
    return status;
}

IO::Types::PositionResult FileWriter::position() const noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    return Detail::Platform::filePosition(*state_);
}

IO::Types::SizeResult FileWriter::size() const noexcept
{
    if (!isOpen())
    {
        return sizeFailure(ErrorCode::NotOpen);
    }

    return Detail::Platform::fileSize(*state_);
}

IO::Types::Status FileWriter::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
{
    if (!isOpen())
    {
        return IO::makeStatus(ErrorCode::NotOpen);
    }

    return Detail::Platform::seekFile(*state_, offset, origin);
}

Types::Lock::Result FileWriter::tryLockExclusive() noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::Lock::Mode::Exclusive);
    if (!result.status.ok() || result.outcome == Types::Lock::Outcome::WouldBlock)
    {
        return {.status = std::move(result.status), .outcome = result.outcome};
    }

    return {
        .status = IO::successStatus(),
        .outcome = Types::Lock::Outcome::Acquired,
        .lock = FileLock(std::move(result.state), Types::Lock::Mode::Exclusive)};
}

File::File() noexcept = default;

File::File(File &&other) noexcept
    : IO::Reader(std::move(static_cast<IO::Reader &>(other)))
    , IO::Writer(std::move(static_cast<IO::Writer &>(other)))
    , state_(std::move(other.state_))
{
}

File::~File() noexcept
{
    static_cast<void>(close());
}

IO::Types::Status File::open(const Types::Path &path, const Types::File::OpenOptions &options) noexcept
{
    if (isOpen())
    {
        return IO::makeStatus(ErrorCode::AlreadyOpen);
    }
    if (!isValidFileAccess(options.access) || !isValidFileOpenMode(options.mode) || !isValidInitialPosition(options.initialPosition) ||
        !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) || !IO::isValidFlushMode(options.flushOnClose))
    {
        return IO::makeStatus(ErrorCode::InvalidArgument);
    }
    if (modeRequiresWrite(options.mode) && !opensForWrite(options.access))
    {
        return IO::makeStatus(ErrorCode::InvalidArgument);
    }
    if (options.flushOnClose != IO::Types::FlushMode::None && !opensForWrite(options.access))
    {
        return IO::makeStatus(ErrorCode::InvalidArgument);
    }

    IO::Types::Status parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);
    if (!parentStatus.ok())
    {
        return parentStatus;
    }

    std::unique_ptr<Detail::FileState> newState;
    IO::Types::Status status = Detail::Platform::openFile(newState, path, options);
    if (status.ok())
    {
        state_ = std::move(newState);
    }
    return status;
}

bool File::isOpen() const noexcept
{
    return stateIsOpen(state_);
}

bool File::canSeek() const noexcept
{
    return isOpen();
}

Types::File::Access File::access() const noexcept
{
    return isOpen() ? state_->access : Types::File::Access::ReadWrite;
}

IO::Types::ReadResult File::read(std::span<std::byte> destination) noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    return Detail::Platform::readFile(*state_, destination);
}

IO::Types::WriteResult File::write(std::span<const std::byte> bytes) noexcept
{
    if (!isOpen())
    {
        return writeFailure(ErrorCode::NotOpen);
    }

    return Detail::Platform::writeFile(*state_, bytes);
}

IO::Types::Status File::flush(IO::Types::FlushMode mode) noexcept
{
    if (!isOpen())
    {
        return IO::makeStatus(ErrorCode::NotOpen);
    }

    return Detail::Platform::flushFile(*state_, mode);
}

IO::Types::Status File::close() noexcept
{
    if (!isOpen())
    {
        state_.reset();
        return IO::successStatus();
    }

    IO::Types::Status status = Detail::Platform::closeFile(*state_);
    if (status.ok())
    {
        state_.reset();
    }
    return status;
}

IO::Types::PositionResult File::position() const noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    return Detail::Platform::filePosition(*state_);
}

IO::Types::SizeResult File::size() const noexcept
{
    if (!isOpen())
    {
        return sizeFailure(ErrorCode::NotOpen);
    }

    return Detail::Platform::fileSize(*state_);
}

IO::Types::Status File::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
{
    if (!isOpen())
    {
        return IO::makeStatus(ErrorCode::NotOpen);
    }

    return Detail::Platform::seekFile(*state_, offset, origin);
}

IO::Types::Status File::resize(std::uint64_t sizeBytes) noexcept
{
    if (!isOpen())
    {
        return IO::makeStatus(ErrorCode::NotOpen);
    }

    return Detail::Platform::resizeFile(*state_, sizeBytes);
}

Types::Lock::Result File::tryLockShared() noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::Lock::Mode::Shared);
    if (!result.status.ok() || result.outcome == Types::Lock::Outcome::WouldBlock)
    {
        return {.status = std::move(result.status), .outcome = result.outcome};
    }

    return {
        .status = IO::successStatus(),
        .outcome = Types::Lock::Outcome::Acquired,
        .lock = FileLock(std::move(result.state), Types::Lock::Mode::Shared)};
}

Types::Lock::Result File::tryLockExclusive() noexcept
{
    if (!isOpen())
    {
        return {.status = IO::makeStatus(ErrorCode::NotOpen)};
    }

    Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::Lock::Mode::Exclusive);
    if (!result.status.ok() || result.outcome == Types::Lock::Outcome::WouldBlock)
    {
        return {.status = std::move(result.status), .outcome = result.outcome};
    }

    return {
        .status = IO::successStatus(),
        .outcome = Types::Lock::Outcome::Acquired,
        .lock = FileLock(std::move(result.state), Types::Lock::Mode::Exclusive)};
}
