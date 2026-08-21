/// @file filesystem_metadata.inl
/// @brief FileSystem entry predicates and metadata queries.

Types::BoolResult exists(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);

        if (result.status.code == ErrorCode::NotFound)
        {
            return {.status = IO::successStatus(), .value = false};
        }
        if (!result.status.ok())
        {
            return boolFailure(std::move(result.status));
        }

        return {.status = IO::successStatus(), .value = true};
    }
    catch (const std::bad_alloc &)
    {
        return boolFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return boolFailure(ErrorCode::Unknown);
    }
}

Types::EntryInfoResult getEntryInfo(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
        if (!result.status.ok())
        {
            return entryInfoFailure(std::move(result.status));
        }

        return {.status = IO::successStatus(), .info = result.info};
    }
    catch (const std::bad_alloc &)
    {
        return entryInfoFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return entryInfoFailure(ErrorCode::Unknown);
    }
}

Types::BoolResult isRegularFile(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
        if (result.status.code == ErrorCode::NotFound)
        {
            return {.status = IO::successStatus(), .value = false};
        }
        if (!result.status.ok())
        {
            return boolFailure(std::move(result.status));
        }
        return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::RegularFile};
    }
    catch (const std::bad_alloc &)
    {
        return boolFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return boolFailure(ErrorCode::Unknown);
    }
}

Types::BoolResult isDirectory(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
        if (result.status.code == ErrorCode::NotFound)
        {
            return {.status = IO::successStatus(), .value = false};
        }
        if (!result.status.ok())
        {
            return boolFailure(std::move(result.status));
        }
        return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::Directory};
    }
    catch (const std::bad_alloc &)
    {
        return boolFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return boolFailure(ErrorCode::Unknown);
    }
}

Types::BoolResult isSymlink(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
        if (result.status.code == ErrorCode::NotFound)
        {
            return {.status = IO::successStatus(), .value = false};
        }
        if (!result.status.ok())
        {
            return boolFailure(std::move(result.status));
        }
        return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::Symlink};
    }
    catch (const std::bad_alloc &)
    {
        return boolFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return boolFailure(ErrorCode::Unknown);
    }
}

IO::Types::SizeResult getFileSize(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Types::EntryInfoResult info = getEntryInfo(path, options);
        if (!info.status.ok())
        {
            return sizeFailure(std::move(info.status));
        }
        if (info.info.kind != Types::EntryKind::RegularFile || !info.info.hasSize)
        {
            return sizeFailure(ErrorCode::InvalidArgument);
        }

        return {.status = IO::successStatus(), .sizeBytes = info.info.sizeBytes};
    }
    catch (const std::bad_alloc &)
    {
        return sizeFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return sizeFailure(ErrorCode::Unknown);
    }
}

Types::LastWriteTimeResult getLastWriteTime(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Types::EntryInfoResult info = getEntryInfo(path, options);
        if (!info.status.ok())
        {
            return lastWriteTimeFailure(std::move(info.status));
        }
        if (!info.info.hasLastWriteTime)
        {
            return lastWriteTimeFailure(ErrorCode::StatFailed);
        }

        return {.status = IO::successStatus(), .time = info.info.lastWriteTime};
    }
    catch (const std::bad_alloc &)
    {
        return lastWriteTimeFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return lastWriteTimeFailure(ErrorCode::Unknown);
    }
}

Types::BoolResult isReadOnly(const Types::Path &path, const Types::EntryOptions &options) noexcept
{
    try
    {
        Types::EntryInfoResult info = getEntryInfo(path, options);
        if (!info.status.ok())
        {
            return boolFailure(std::move(info.status));
        }

        return {.status = IO::successStatus(), .value = info.info.readOnly};
    }
    catch (const std::bad_alloc &)
    {
        return boolFailure(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return boolFailure(ErrorCode::Unknown);
    }
}
