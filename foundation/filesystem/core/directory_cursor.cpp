/// @file directory_cursor.cpp
/// @brief FileSystem directory cursor operations.

#include "filesystem/internal/platform.h"
#include "filesystem/internal/helpers.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Internal;

    /// ------------------------------------------------------------
    /// Directory cursor
    /// ------------------------------------------------------------

    DirectoryCursor::DirectoryCursor() noexcept = default;

    DirectoryCursor::DirectoryCursor(DirectoryCursor &&other) noexcept = default;

    DirectoryCursor &DirectoryCursor::operator=(DirectoryCursor &&other) noexcept = default;

    DirectoryCursor::~DirectoryCursor() noexcept = default;

    GameWIP::Status::Types::Result<void> DirectoryCursor::open(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
    {
        try
        {
            // Validate the cursor state and request before opening the native cursor.
            if(state_)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyOpen));
            }

            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            auto opened = Platform::openDirectoryCursor(path, options.symlinkPolicy);

            if(!opened.ok())
            {
                return GameWIP::Status::Types::Result<void>::failure(opened.status());
            }

            // Publish the cursor state only after the platform open succeeds.
            options_ = options;
            emittedEntries_ = 0;
            limitReached_ = false;
            state_ = std::move(opened).value();
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

    bool DirectoryCursor::isOpen() const noexcept
    {
        return state_ != nullptr;
    }

    GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>> DirectoryCursor::next() noexcept
    {
        try
        {
            // Reject closed cursors and repeated calls after the entry limit.
            if(!state_)
            {
                return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::failure(
                    GameWIP::Status::Types::Status(ErrorCode::NotOpen));
            }

            if(limitReached_)
            {
                return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::failure(
                    GameWIP::Status::Types::Status(ErrorCode::SizeLimitExceeded));
            }

            // Skip entries excluded by the caller's listing options.
            while(true)
            {
                auto next = Platform::readDirectoryCursor(*state_);

                if(!next.ok())
                {
                    return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::failure(next.status());
                }

                auto nextEntry = std::move(next).value();

                if(!nextEntry)
                {
                    return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::success(std::nullopt);
                }

                Platform::DirectoryCursorEntry &candidate = *nextEntry;
                if((!options_.includeHidden && candidate.hidden) || !includeEntryKind(candidate.entry.info.kind, options_))
                {
                    continue;
                }

                // Enforce the entry limit before returning the next visible item.
                if(emittedEntries_ >= options_.maxEntries)
                {
                    limitReached_ = true;
                    return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::failure(
                        GameWIP::Status::Types::Status(ErrorCode::SizeLimitExceeded));
                }

                ++emittedEntries_;
                return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::success(std::move(candidate.entry));
            }
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::failure(
                GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<std::optional<Types::Directory::Entry>>::failure(
                GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<void> DirectoryCursor::close() noexcept
    {
        state_.reset();
        emittedEntries_ = 0;
        limitReached_ = false;
        return GameWIP::Status::Types::Result<void>::success();
    }

} // namespace GameWIP::FileSystem
