/// @file entries.cpp
/// @brief Win32 FileSystem entry queries and mutations.

#include "filesystem/internal/filesystem_platform.h"
#include "filesystem/platform/win32/internal/helpers.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem::Detail::Platform
{
    using namespace Win32;

    EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            return queryEntryImpl(path, symlinkPolicy);
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    /// ------------------------------------------------------------
    /// Metadata and mutations
    /// ------------------------------------------------------------

    IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            OpenPathResult opened =
                openExistingPath(path, symlinkPolicy, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, kShareAll, true, ErrorCode::StatFailed);

            if(!opened.status.ok())
            {
                return std::move(opened.status);
            }
            return setHandleReadOnly(opened.handle.get(), readOnly);
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

    IO::Types::Status copyBasicMetadata(const Types::Path &from, const Types::Path &to, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(from.empty() || to.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            OpenPathResult source = openExistingPath(from, symlinkPolicy, FILE_READ_ATTRIBUTES, kShareAll, false, ErrorCode::StatFailed);

            if(!source.status.ok())
            {
                return std::move(source.status);
            }

            if(source.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            OpenPathResult destination = openExistingPath(
                to,
                Types::SymlinkPolicy::DoNotFollow,
                FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                kShareAll,
                false,
                ErrorCode::StatFailed);

            if(!destination.status.ok())
            {
                return std::move(destination.status);
            }

            if(destination.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            return copyHandleBasicMetadata(source.handle.get(), destination.handle.get());
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
            OpenPathResult opened =
                openExistingPath(path, options.symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::RemoveFailed);

            if(!opened.status.ok())
            {
                if(opened.status.code == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return IO::successStatus();
                }
                return std::move(opened.status);
            }

            if(opened.info.kind == Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::IsDirectory);
            }
            return markHandleForDeletion(opened.handle.get(), ErrorCode::RemoveFailed);
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

    IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            OpenPathResult opened =
                openExistingPath(path, options.symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::RemoveFailed);

            if(!opened.status.ok())
            {
                if(opened.status.code == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return IO::successStatus();
                }
                return std::move(opened.status);
            }

            if(opened.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::NotDirectory);
            }
            return markHandleForDeletion(opened.handle.get(), ErrorCode::RemoveFailed);
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

    IO::Types::Status movePath(
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
                return IO::makeStatus(ErrorCode::InvalidArgument);
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
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            // Use handle-relative rename when traversal cannot follow every symlink.
            if(symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                OpenPathResult source = openExistingPath(from, symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::MoveFailed);

                if(!source.status.ok())
                {
                    return std::move(source.status);
                }
                WidePathResult nativeTo = absoluteNativePath(to);

                if(!nativeTo.status.ok())
                {
                    return std::move(nativeTo.status);
                }
                ParsedPathResult parsedTo = parseAbsolutePath(nativeTo.path);

                if(!parsedTo.status.ok())
                {
                    return std::move(parsedTo.status);
                }

                if(parsedTo.components.empty())
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                HandleResult destinationParent = openParentStrict(parsedTo);

                if(!destinationParent.status.ok())
                {
                    return std::move(destinationParent.status);
                }
#if FILESYSTEM_INTERNAL_TEST_HOOKS
                pauseMoveIfArmed(false);
#endif

                // Keep the validated parent handle authoritative through commit. An absolute name
                // would re-resolve every ancestor and reopen the validation-to-rename race.
                const std::wstring &fileName = parsedTo.components.back();
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
                renameInfo->RootDirectory = destinationParent.handle.get();
                renameInfo->FileNameLength = fileNameBytes;
                std::copy(fileName.begin(), fileName.end(), renameInfo->FileName);

                const NtApi &api = ntApi();

                if(api.setInformationFile == nullptr)
                {
                    return IO::makeStatus(ErrorCode::Unsupported, 0, "NtSetInformationFile is unavailable");
                }
                IO_STATUS_BLOCK renameStatus{};
                const NTSTATUS status = api.setInformationFile(
                    source.handle.get(),
                    &renameStatus,
                    renameInfo,
                    static_cast<ULONG>(renameBuffer.size()),
                    FileRenameInformation);

                if(!NT_SUCCESS(status))
                {
                    return makeNtStatus(status, ErrorCode::MoveFailed);
                }
#if FILESYSTEM_INTERNAL_TEST_HOOKS
                pauseMoveIfArmed(true);
#endif
                return IO::successStatus();
            }

            // Use ordinary Win32 path resolution when all symlinks may be followed.
            WidePathResult nativeFrom = absoluteNativePath(from);

            if(!nativeFrom.status.ok())
            {
                return std::move(nativeFrom.status);
            }
            WidePathResult nativeTo = absoluteNativePath(to);

            if(!nativeTo.status.ok())
            {
                return std::move(nativeTo.status);
            }

            if(MoveFileExW(nativeFrom.path.c_str(), nativeTo.path.c_str(), flags) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::MoveFailed);
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

    IO::Types::Status flushDirectory(const Types::Path &path) noexcept
    {
        try
        {
            if(path.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            OpenPathResult opened = openExistingPath(
                path,
                Types::SymlinkPolicy::FollowAll,
                FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                true,
                ErrorCode::FlushFailed);

            if(!opened.status.ok())
            {
                return std::move(opened.status);
            }

            if(opened.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::NotDirectory);
            }

            if(FlushFileBuffers(opened.handle.get()) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::FlushFailed);
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

    Types::BoolResult isHidden(const Types::Path &path) noexcept
    {
        try
        {
            WidePathResult nativePath = absoluteNativePath(path);

            if(!nativePath.status.ok())
            {
                return {.status = std::move(nativePath.status), .value = false};
            }
            const DWORD attributes = GetFileAttributesW(nativePath.path.c_str());

            if(attributes == INVALID_FILE_ATTRIBUTES)
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed), .value = false};
            }

            return {.status = IO::successStatus(), .value = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0};
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory), .value = false};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown), .value = false};
        }
    }

} // namespace GameWIP::FileSystem::Detail::Platform
