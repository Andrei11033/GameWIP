/// @file mutations.cpp
/// @brief FileSystem resize, copy, move, and mutation operations.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#include <cstddef>
#include <filesystem>
#include <new>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace GameWIP::FileSystem
{
    using namespace Internal;

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

    GameWIP::Status::Types::Result<void> resizeFile(const Types::Path &path, std::uint64_t sizeBytes, const Types::File::ResizeOptions &options) noexcept
    {
        try
        {
            File file;
            auto openResult = file.open(
                path,
                Types::File::OpenOptions{
                    .access = Types::File::Access::Write,
                    .mode = Types::File::OpenMode::OpenExisting,
                    .symlinkPolicy = options.symlinkPolicy});

            if(!openResult.ok())
            {
                return openResult;
            }

            // Resize without restoring the private handle position.
            auto resizeResult = file.resizeWithoutPositionRestore(sizeBytes);
            auto closeResult = file.close();

            if(!resizeResult.ok())
            {
                return resizeResult;
            }

            return closeResult;
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<void> truncateFile(const Types::Path &path, const Types::File::ResizeOptions &options) noexcept
    {
        return resizeFile(path, 0, options);
    }

    GameWIP::Status::Types::Result<void> setReadOnly(const Types::Path &path, bool readOnly, const Types::EntryOptions &options) noexcept
    {
        try
        {
            return Platform::setReadOnly(path, readOnly, options.symlinkPolicy);
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    /// ------------------------------------------------------------
    /// File copy
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> copyFile(const Types::Path &from, const Types::Path &to, const Types::File::CopyOptions &options) noexcept
    {
        try
        {
            // Validate the source and destination policy before opening either handle.
            if(from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
               !isValidCopyMetadataMode(options.metadataMode) || !IO::isValidFlushMode(options.flushMode))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            auto source = getEntryInfo(from, Types::EntryOptions{.symlinkPolicy = options.symlinkPolicy});

            if(!source.ok())
            {
                return GameWIP::Status::Types::Result<void>::failure(source.status());
            }

            if(source.value().kind != Types::EntryKind::RegularFile)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            if(equivalentOrSameLexically(from, to))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            auto destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);

            if(destination.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyExists));
            }

            if(!destination.ok() && destination.status().code() != ErrorCode::NotFound)
            {
                return GameWIP::Status::Types::Result<void>::failure(destination.status());
            }

            auto parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);

            if(!parentStatus.ok())
            {
                return parentStatus;
            }

            // Open the source before creating or truncating the destination.
            FileReader reader;
            auto readerOpenResult =
                reader.open(from, Types::File::ReaderOpenOptions{.share = Types::File::Share::All, .symlinkPolicy = options.symlinkPolicy});

            if(!readerOpenResult.ok())
            {
                return readerOpenResult;
            }

            // Copy is non-transactional after the destination opens.
            const Types::File::WriterMode writerMode = options.replaceMode == Types::ReplaceMode::ReplaceExisting
                                                           ? Types::File::WriterMode::CreateOrTruncate
                                                           : Types::File::WriterMode::CreateNew;
            FileWriter writer;
            auto writerOpenResult = writer.open(
                to,
                Types::File::WriterOpenOptions{
                    .mode = writerMode,
                    .share = Types::File::Share::None,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = false});

            if(!writerOpenResult.ok())
            {
                static_cast<void>(reader.close());
                return writerOpenResult;
            }

            // Copy through a bounded buffer and preserve the first I/O failure.
            std::vector<std::byte> buffer(IO::kDefaultBufferSize);
            std::uint64_t copiedBytes = 0;

            while(true)
            {
                auto readResult = reader.read(buffer);

                if(!readResult.ok())
                {
                    static_cast<void>(writer.close());
                    static_cast<void>(reader.close());
                    return GameWIP::Status::Types::Result<void>::failure(readResult.status());
                }

                if(readResult.value().bytesRead > 0)
                {
                    auto writeResult = IO::writeAllBytes(writer, std::span{buffer}.first(readResult.value().bytesRead));

                    if(!writeResult.ok())
                    {
                        static_cast<void>(writer.close());
                        static_cast<void>(reader.close());
                        return GameWIP::Status::Types::Result<void>::failure(writeResult.status());
                    }

                    copiedBytes += writeResult.value().bytesWritten;
                }

                if(readResult.value().endOfStream)
                {
                    break;
                }
            }

            // A changing source is not a coherent snapshot; reject a byte-count mismatch rather than reporting a misleading successful copy.
            if(source.value().hasSize && copiedBytes != source.value().sizeBytes)
            {
                static_cast<void>(writer.close());
                static_cast<void>(reader.close());
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::CopyFailed));
            }

            // Close both streams before applying optional metadata.
            auto flushResult = writer.flush(options.flushMode);
            auto writerCloseResult = writer.close();
            auto readerCloseResult = reader.close();

            if(!flushResult.ok())
            {
                return flushResult;
            }

            if(!writerCloseResult.ok())
            {
                return writerCloseResult;
            }

            if(!readerCloseResult.ok())
            {
                return readerCloseResult;
            }

            if(options.metadataMode == Types::File::CopyMetadataMode::Basic)
            {
                return Platform::copyBasicMetadata(from, to, options.symlinkPolicy);
            }

            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    /// ------------------------------------------------------------
    /// Move and remove
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> movePath(const Types::Path &from, const Types::Path &to, const Types::MoveOptions &options) noexcept
    {
        try
        {
            // Validate the request before querying either path.
            if(from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            // Resolve the source under the selected symlink policy.
            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(from))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unsupported));
            }

            auto source = getEntryInfo(from, Types::EntryOptions{.symlinkPolicy = options.symlinkPolicy});

            if(!source.ok())
            {
                return GameWIP::Status::Types::Result<void>::failure(source.status());
            }

            // A move to the same entry is already complete.
            if(equivalentOrSameLexically(from, to))
            {
                return GameWIP::Status::Types::Result<void>::success();
            }

            // Check destination conflicts before creating its parent or moving.
            auto destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);

            if(destination.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyExists));
            }

            if(!destination.ok() && destination.status().code() != ErrorCode::NotFound)
            {
                return GameWIP::Status::Types::Result<void>::failure(destination.status());
            }

            auto parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);

            if(!parentStatus.ok())
            {
                return parentStatus;
            }

            auto moveResult = Platform::movePath(from, to, options.replaceMode, options.symlinkPolicy);

            if(!moveResult.ok())
            {
                return moveResult;
            }

            // Native rename success is final even if the names change afterward.
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<void> removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unsupported));
            }

            return Platform::removeFile(path, options);
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }
} // namespace GameWIP::FileSystem
