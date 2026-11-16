/// @file mutations.cpp
/// @brief FileSystem resize, copy, move, and mutation operations.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/filesystem_platform.h"

#include <cstddef>
#include <filesystem>
#include <new>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace GameWIP::FileSystem
{
    using namespace Detail;

    namespace
    {
        // Copy option validation

        [[nodiscard]] bool isValidCopyMetadataMode(Types::File::CopyMetadataMode mode) noexcept
        {
            switch(mode)
            {
            case Types::File::CopyMetadataMode::None:
            case Types::File::CopyMetadataMode::Basic:
                return true;
            }

            return false;
        }

        // Source and destination comparison

        [[nodiscard]] bool equivalentOrSameLexically(const Types::Path &left, const Types::Path &right) noexcept
        {
            if(left == right)
            {
                return true;
            }

            std::error_code ec;
            const bool equivalent = std::filesystem::equivalent(left, right, ec);
            return !ec && equivalent;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Size and attributes
    /// ------------------------------------------------------------

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

            if(!openStatus.ok())
            {
                return openStatus;
            }

            // Resize without restoring the private handle position.
            IO::Types::Status resizeStatus = file.resizeWithoutPositionRestore(sizeBytes);
            IO::Types::Status closeStatus = file.close();

            if(!resizeStatus.ok())
            {
                return resizeStatus;
            }

            return closeStatus;
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
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
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// File copy
    /// ------------------------------------------------------------

    IO::Types::Status copyFile(const Types::Path &from, const Types::Path &to, const Types::File::CopyOptions &options) noexcept
    {
        try
        {
            // Validate the source and destination policy before opening either handle.
            if(from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
               !isValidCopyMetadataMode(options.metadataMode) || !IO::isValidFlushMode(options.flushMode))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            Types::EntryInfoResult source = getEntryInfo(from, Types::EntryOptions{.symlinkPolicy = options.symlinkPolicy});

            if(!source.status.ok())
            {
                return std::move(source.status);
            }

            if(source.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(equivalentOrSameLexically(from, to))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            Detail::Platform::EntryQueryResult destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);

            if(destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }

            if(!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
            {
                return std::move(destination.status);
            }

            IO::Types::Status parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);

            if(!parentStatus.ok())
            {
                return parentStatus;
            }

            // Open the source before creating or truncating the destination.
            FileReader reader;
            IO::Types::Status readerOpenStatus =
                reader.open(from, Types::File::ReaderOpenOptions{.share = Types::File::Share::All, .symlinkPolicy = options.symlinkPolicy});

            if(!readerOpenStatus.ok())
            {
                return readerOpenStatus;
            }

            // Copy is non-transactional after the destination opens.
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

            if(!writerOpenStatus.ok())
            {
                static_cast<void>(reader.close());
                return writerOpenStatus;
            }

            // Copy through a bounded buffer and preserve the first I/O failure.
            std::vector<std::byte> buffer(IO::kDefaultBufferSize);
            std::uint64_t copiedBytes = 0;

            while(true)
            {
                IO::Types::ReadResult readResult = reader.read(buffer);

                if(!readResult.status.ok())
                {
                    static_cast<void>(writer.close());
                    static_cast<void>(reader.close());
                    return std::move(readResult.status);
                }

                if(readResult.bytesRead > 0)
                {
                    IO::Types::WriteResult writeResult = IO::writeAllBytes(writer, std::span{buffer}.first(readResult.bytesRead));

                    if(!writeResult.status.ok())
                    {
                        static_cast<void>(writer.close());
                        static_cast<void>(reader.close());
                        return std::move(writeResult.status);
                    }

                    copiedBytes += writeResult.bytesWritten;
                }

                if(readResult.endOfStream)
                {
                    break;
                }
            }

            // A changing source is not a coherent snapshot; reject a byte-count mismatch rather than reporting a misleading successful copy.
            if(source.info.hasSize && copiedBytes != source.info.sizeBytes)
            {
                static_cast<void>(writer.close());
                static_cast<void>(reader.close());
                return IO::makeStatus(ErrorCode::CopyFailed);
            }

            // Close both streams before applying optional metadata.
            IO::Types::Status flushStatus = writer.flush(options.flushMode);
            IO::Types::Status writerCloseStatus = writer.close();
            IO::Types::Status readerCloseStatus = reader.close();

            if(!flushStatus.ok())
            {
                return flushStatus;
            }

            if(!writerCloseStatus.ok())
            {
                return writerCloseStatus;
            }

            if(!readerCloseStatus.ok())
            {
                return readerCloseStatus;
            }

            if(options.metadataMode == Types::File::CopyMetadataMode::Basic)
            {
                return Detail::Platform::copyBasicMetadata(from, to, options.symlinkPolicy);
            }

            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Move and remove
    /// ------------------------------------------------------------

    IO::Types::Status movePath(const Types::Path &from, const Types::Path &to, const Types::MoveOptions &options) noexcept
    {
        try
        {
            // Validate the request before querying either path.
            if(from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            // Resolve the source under the selected symlink policy.
            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(from))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            Types::EntryInfoResult source = getEntryInfo(from, Types::EntryOptions{.symlinkPolicy = options.symlinkPolicy});

            if(!source.status.ok())
            {
                return std::move(source.status);
            }

            // A move to the same entry is already complete.
            if(equivalentOrSameLexically(from, to))
            {
                return IO::successStatus();
            }

            // Check destination conflicts before creating its parent or moving.
            Detail::Platform::EntryQueryResult destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);

            if(destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }

            if(!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
            {
                return std::move(destination.status);
            }

            IO::Types::Status parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);

            if(!parentStatus.ok())
            {
                return parentStatus;
            }

            IO::Types::Status moveStatus = Detail::Platform::movePath(from, to, options.replaceMode, options.symlinkPolicy);

            if(!moveStatus.ok())
            {
                return moveStatus;
            }

            // Native rename success is final even if the names change afterward.
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            return Detail::Platform::removeFile(path, options);
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::FileSystem
