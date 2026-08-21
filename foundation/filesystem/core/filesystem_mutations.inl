/// @file filesystem_mutations.inl
/// @brief FileSystem resize, copy, move, and mutation operations.

IO::Types::Status resizeFile(const Types::Path &path, std::uint64_t sizeBytes, const Types::File::ResizeOptions &options) noexcept
{
    try
    {
        File file;
        IO::Types::Status openStatus = file.open(
            path,
            Types::File::OpenOptions{
                .access = Types::File::Access::Write,
                .mode = Types::File::OpenMode::OpenExisting,
                .symlinkPolicy = options.symlinkPolicy});
        if (!openStatus.ok())
        {
            return openStatus;
        }

        IO::Types::Status resizeStatus = file.resize(sizeBytes);
        IO::Types::Status closeStatus = file.close();
        if (!resizeStatus.ok())
        {
            return resizeStatus;
        }
        return closeStatus;
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

IO::Types::Status truncateFile(const Types::Path &path, const Types::File::ResizeOptions &options) noexcept
{
    return resizeFile(path, 0, options);
}

IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, const Types::EntryOptions &options) noexcept
{
    try
    {
        return Detail::Platform::setReadOnly(path, readOnly, options.symlinkPolicy);
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

IO::Types::Status copyFile(const Types::Path &from, const Types::Path &to, const Types::File::CopyOptions &options) noexcept
{
    try
    {
        if (from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
            !isValidCopyMetadataMode(options.metadataMode) || !IO::isValidFlushMode(options.flushMode))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        Types::EntryInfoResult source = getEntryInfo(from, Types::EntryOptions{.symlinkPolicy = options.symlinkPolicy});
        if (!source.status.ok())
        {
            return std::move(source.status);
        }
        if (source.info.kind != Types::EntryKind::RegularFile)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }
        if (equivalentOrSameLexically(from, to))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        Detail::Platform::EntryQueryResult destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);
        if (destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
        {
            return IO::makeStatus(ErrorCode::AlreadyExists);
        }
        if (!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
        {
            return std::move(destination.status);
        }

        IO::Types::Status parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);
        if (!parentStatus.ok())
        {
            return parentStatus;
        }

        FileReader reader;
        IO::Types::Status readerOpenStatus =
            reader.open(from, Types::File::ReaderOpenOptions{.share = Types::File::Share::All, .symlinkPolicy = options.symlinkPolicy});
        if (!readerOpenStatus.ok())
        {
            return readerOpenStatus;
        }

        const Types::File::WriterMode writerMode = options.replaceMode == Types::ReplaceMode::ReplaceExisting
                                                       ? Types::File::WriterMode::CreateOrTruncate
                                                       : Types::File::WriterMode::CreateNew;
        FileWriter writer;
        IO::Types::Status writerOpenStatus = writer.open(
            to,
            Types::File::WriterOpenOptions{
                .mode = writerMode,
                .share = Types::File::Share::None,
                .symlinkPolicy = options.symlinkPolicy,
                .createParentDirectories = false});
        if (!writerOpenStatus.ok())
        {
            static_cast<void>(reader.close());
            return writerOpenStatus;
        }

        std::vector<std::byte> buffer(IO::kDefaultBufferSize);
        std::uint64_t copiedBytes = 0;
        while (true)
        {
            IO::Types::ReadResult readResult = reader.read(std::span<std::byte>(buffer.data(), buffer.size()));
            if (!readResult.status.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(reader.close());
                return std::move(readResult.status);
            }

            if (readResult.bytesRead > 0)
            {
                IO::Types::WriteResult writeResult = IO::writeAllBytes(writer, std::span<const std::byte>(buffer.data(), readResult.bytesRead));
                if (!writeResult.status.ok())
                {
                    static_cast<void>(writer.close());
                    static_cast<void>(reader.close());
                    return std::move(writeResult.status);
                }
                copiedBytes += writeResult.bytesWritten;
            }

            if (readResult.endOfStream)
            {
                break;
            }
        }

        // A changing source is not a coherent snapshot; reject a byte-count mismatch rather than reporting a misleading successful copy.
        if (source.info.hasSize && copiedBytes != source.info.sizeBytes)
        {
            static_cast<void>(writer.close());
            static_cast<void>(reader.close());
            return IO::makeStatus(ErrorCode::CopyFailed);
        }

        IO::Types::Status flushStatus = writer.flush(options.flushMode);
        IO::Types::Status writerCloseStatus = writer.close();
        IO::Types::Status readerCloseStatus = reader.close();
        if (!flushStatus.ok())
        {
            return flushStatus;
        }
        if (!writerCloseStatus.ok())
        {
            return writerCloseStatus;
        }
        if (!readerCloseStatus.ok())
        {
            return readerCloseStatus;
        }

        if (options.metadataMode == Types::File::CopyMetadataMode::Basic)
        {
            return copyBasicMetadata(from, to, options.symlinkPolicy);
        }

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

IO::Types::Status movePath(const Types::Path &from, const Types::Path &to, const Types::MoveOptions &options) noexcept
{
    try
    {
        if (from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }
        if (options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(from))
        {
            return IO::makeStatus(ErrorCode::Unsupported);
        }

        Types::EntryInfoResult source = getEntryInfo(from, Types::EntryOptions{.symlinkPolicy = options.symlinkPolicy});
        if (!source.status.ok())
        {
            return std::move(source.status);
        }
        // A native move to the same entry is already satisfied; unlike copy, no destination content would be produced by doing work.
        if (equivalentOrSameLexically(from, to))
        {
            return IO::successStatus();
        }
        Detail::Platform::EntryQueryResult destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);
        if (destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
        {
            return IO::makeStatus(ErrorCode::AlreadyExists);
        }
        if (!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
        {
            return std::move(destination.status);
        }

        IO::Types::Status parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);
        if (!parentStatus.ok())
        {
            return parentStatus;
        }

        IO::Types::Status moveStatus = Detail::Platform::movePath(from, to, options.replaceMode, options.symlinkPolicy);
        if (!moveStatus.ok())
        {
            return moveStatus;
        }
        // Native success is the move's linearization point. Re-querying either name here could
        // turn a completed move into an apparent failure when another actor recreates the
        // source or removes the destination, making a caller's retry unsafe.
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

IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept
{
    try
    {
        if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }
        if (options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
        {
            return IO::makeStatus(ErrorCode::Unsupported);
        }

        return Detail::Platform::removeFile(path, options);
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
