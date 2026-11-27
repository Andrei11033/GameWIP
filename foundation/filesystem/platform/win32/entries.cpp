/// @file entries.cpp
/// @brief Win32 FileSystem entry queries and mutations.

#include "filesystem/internal/platform.h"
#include "filesystem/platform/win32/internal/helpers.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem::Platform
{
    using namespace Internal;
    using namespace Win32;

    /// ------------------------------------------------------------
    /// Entry queries
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<Types::EntryInfo> queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            return queryEntryImpl(path, symlinkPolicy);
        }
        catch(const std::bad_alloc &)
        {
            return failure<Types::EntryInfo>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<Types::EntryInfo>(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Entry metadata
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> setReadOnly(const Types::Path &path, bool readOnly, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            auto opened = openExistingPath(path, symlinkPolicy, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, kShareAll, true, ErrorCode::StatFailed);

            if(!opened.ok())
            {
                return failure<void>(opened.status());
            }

            return setHandleReadOnly(opened.value().handle.get(), readOnly);
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> copyBasicMetadata(
        const Types::Path &from,
        const Types::Path &to,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(from.empty() || to.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            auto source = openExistingPath(from, symlinkPolicy, FILE_READ_ATTRIBUTES, kShareAll, false, ErrorCode::StatFailed);

            if(!source.ok())
            {
                return failure<void>(source.status());
            }

            if(source.value().info.kind != Types::EntryKind::RegularFile)
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            auto destination = openExistingPath(
                to,
                Types::SymlinkPolicy::DoNotFollow,
                FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                kShareAll,
                false,
                ErrorCode::StatFailed);

            if(!destination.ok())
            {
                return failure<void>(destination.status());
            }

            if(destination.value().info.kind != Types::EntryKind::RegularFile)
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            return copyHandleBasicMetadata(source.value().handle.get(), destination.value().handle.get());
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Entry mutations
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            auto opened = openExistingPath(path, options.symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::RemoveFailed);

            if(!opened.ok())
            {
                if(opened.status().code() == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return success();
                }
                return failure<void>(opened.status());
            }

            if(opened.value().info.kind == Types::EntryKind::Directory)
            {
                return failure<void>(ErrorCode::IsDirectory);
            }

            return markHandleForDeletion(opened.value().handle.get(), ErrorCode::RemoveFailed);
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            auto opened = openExistingPath(path, options.symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::RemoveFailed);

            if(!opened.ok())
            {
                if(opened.status().code() == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return success();
                }
                return failure<void>(opened.status());
            }

            if(opened.value().info.kind != Types::EntryKind::Directory)
            {
                return failure<void>(ErrorCode::NotDirectory);
            }

            return markHandleForDeletion(opened.value().handle.get(), ErrorCode::RemoveFailed);
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> movePath(
        const Types::Path &from,
        const Types::Path &to,
        Types::ReplaceMode replaceMode,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            // Validate paths and symlink policy before selecting the move path.
            if(from.empty() || to.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            DWORD flags = MOVEFILE_WRITE_THROUGH;

            // Map the replacement policy to Win32 move flags.
            switch(replaceMode)
            {
            case Types::ReplaceMode::FailIfExists:
                break;
            case Types::ReplaceMode::ReplaceExisting:
                flags |= MOVEFILE_REPLACE_EXISTING;
                break;
            default:
                return failure<void>(ErrorCode::InvalidArgument);
            }

            // Use handle-relative rename when traversal cannot follow every symlink.
            if(symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                auto source = openExistingPath(from, symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::MoveFailed);

                if(!source.ok())
                {
                    return failure<void>(source.status());
                }

                // Resolve the destination and keep its parent open through commit.
                auto nativeTo = absoluteNativePath(to);

                if(!nativeTo.ok())
                {
                    return failure<void>(nativeTo.status());
                }

                auto parsedTo = parseAbsolutePath(nativeTo.value());

                if(!parsedTo.ok())
                {
                    return failure<void>(parsedTo.status());
                }

                if(parsedTo.value().components.empty())
                {
                    return failure<void>(ErrorCode::InvalidArgument);
                }

                auto destinationParent = openParentStrict(parsedTo.value());

                if(!destinationParent.ok())
                {
                    return failure<void>(destinationParent.status());
                }

#if FILESYSTEM_INTERNAL_TEST_HOOKS
                pauseMoveIfArmed(false);
#endif

                // Keep the validated parent handle authoritative through commit. An absolute name
                // would re-resolve every ancestor and reopen the validation-to-rename race.
                const std::wstring &fileName = parsedTo.value().components.back();
                const DWORD fileNameBytes = static_cast<DWORD>(fileName.size() * sizeof(wchar_t));
                std::vector<std::byte> renameBuffer(sizeof(FILE_RENAME_INFO) + fileNameBytes + sizeof(wchar_t));
                // renameBuffer owns the complete fixed header, variable file name, and terminator passed to NtSetInformationFile.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
                auto *renameInfo = reinterpret_cast<FILE_RENAME_INFO *>(renameBuffer.data());
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
                renameInfo->ReplaceIfExists = replaceMode == Types::ReplaceMode::ReplaceExisting;
                renameInfo->RootDirectory = destinationParent.value().handle.get();
                renameInfo->FileNameLength = fileNameBytes;
                std::copy(fileName.begin(), fileName.end(), renameInfo->FileName);

                const NtApi &api = ntApi();

                if(api.setInformationFile == nullptr)
                {
                    return failure<void>(Status::withDiagnostic(ErrorCode::Unsupported, "NtSetInformationFile is unavailable"));
                }

                IO_STATUS_BLOCK renameStatus{};
                const NTSTATUS status = api.setInformationFile(
                    source.value().handle.get(),
                    &renameStatus,
                    renameInfo,
                    static_cast<ULONG>(renameBuffer.size()),
                    FileRenameInformation);

                if(!NT_SUCCESS(status))
                {
                    return failure<void>(makeNtStatus(status, ErrorCode::MoveFailed));
                }

#if FILESYSTEM_INTERNAL_TEST_HOOKS
                pauseMoveIfArmed(true);
#endif

                return success();
            }

            // Use ordinary Win32 path resolution when all symlinks may be followed.
            auto nativeFrom = absoluteNativePath(from);

            if(!nativeFrom.ok())
            {
                return failure<void>(nativeFrom.status());
            }

            auto nativeTo = absoluteNativePath(to);

            if(!nativeTo.ok())
            {
                return failure<void>(nativeTo.status());
            }

            if(MoveFileExW(nativeFrom.value().c_str(), nativeTo.value().c_str(), flags) == FALSE)
            {
                return failure<void>(makeLastErrorStatus(ErrorCode::MoveFailed));
            }

            return success();
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Directory durability
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> flushDirectory(const Types::Path &path) noexcept
    {
        try
        {
            if(path.empty())
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            auto opened = openExistingPath(
                path,
                Types::SymlinkPolicy::FollowAll,
                FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                true,
                ErrorCode::FlushFailed);

            if(!opened.ok())
            {
                return failure<void>(opened.status());
            }

            if(opened.value().info.kind != Types::EntryKind::Directory)
            {
                return failure<void>(ErrorCode::NotDirectory);
            }

            if(FlushFileBuffers(opened.value().handle.get()) == FALSE)
            {
                return failure<void>(makeLastErrorStatus(ErrorCode::FlushFailed));
            }

            return success();
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Hidden-entry query
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<bool> isHidden(const Types::Path &path) noexcept
    {
        try
        {
            auto nativePath = absoluteNativePath(path);

            if(!nativePath.ok())
            {
                return failure<bool>(nativePath.status());
            }

            const DWORD attributes = GetFileAttributesW(nativePath.value().c_str());

            if(attributes == INVALID_FILE_ATTRIBUTES)
            {
                return failure<bool>(makeLastErrorStatus(ErrorCode::StatFailed));
            }

            return GameWIP::Status::Types::Result<bool>::success((attributes & FILE_ATTRIBUTE_HIDDEN) != 0);
        }
        catch(const std::bad_alloc &)
        {
            return failure<bool>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<bool>(ErrorCode::Unknown);
        }
    }

} // namespace GameWIP::FileSystem::Platform
