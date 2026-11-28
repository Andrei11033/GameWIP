/// @file validation.cpp
/// @brief Shared FileSystem option and parent-path validation.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem::Internal
{
    /// ------------------------------------------------------------
    /// Shared option validation
    /// ------------------------------------------------------------

    bool isValidReplaceMode(Types::ReplaceMode mode) noexcept
    {
        switch(mode)
        {
        case Types::ReplaceMode::FailIfExists:
        case Types::ReplaceMode::ReplaceExisting:
            return true;
        }

        return false;
    }

    bool isValidFileShare(Types::File::Share share) noexcept
    {
        const std::uint32_t shareBits = static_cast<std::uint8_t>(share);
        const std::uint32_t allowedBits = static_cast<std::uint8_t>(Types::File::Share::All);
        return (shareBits & ~allowedBits) == 0;
    }

    bool isValidSymlinkPolicy(Types::SymlinkPolicy policy) noexcept
    {
        switch(policy)
        {
        case Types::SymlinkPolicy::DoNotFollow:
        case Types::SymlinkPolicy::FollowFinal:
        case Types::SymlinkPolicy::FollowAll:
            return true;
        }

        return false;
    }

    /// ------------------------------------------------------------
    /// Directory entry filtering
    /// ------------------------------------------------------------

    bool includeEntryKind(Types::EntryKind kind, const Types::Directory::ListOptions &options) noexcept
    {
        switch(kind)
        {
        case Types::EntryKind::RegularFile:
            return options.includeFiles;
        case Types::EntryKind::Directory:
            return options.includeDirectories;
        case Types::EntryKind::Symlink:
            return options.includeSymlinks;
        case Types::EntryKind::Other:
            return options.includeOther;
        }

        return false;
    }

    /// ------------------------------------------------------------
    /// Parent-path validation
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> validateParentDirectory(
        const Types::Path &path,
        bool createMissing,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            const Types::Path parent = path.parent_path();

            if(parent.empty())
            {
                return GameWIP::Status::Types::Result<void>::success();
            }

            if(createMissing)
            {
                return createDirectories(parent, Types::Directory::CreateOptions{.succeedIfAlreadyExists = true, .symlinkPolicy = symlinkPolicy});
            }

            auto result = queryEntry(parent, symlinkPolicy);

            if(!result.ok())
            {
                return GameWIP::Status::Types::Result<void>::failure(result.status());
            }

            if(result.value().kind != Types::EntryKind::Directory)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::NotDirectory));
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
} // namespace GameWIP::FileSystem::Internal
