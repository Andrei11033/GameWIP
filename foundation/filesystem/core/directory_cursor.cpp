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

    IO::Types::Status DirectoryCursor::open(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
    {
        try
        {
            // Validate the cursor state and request before opening the native cursor.
            if(state_)
            {
                return IO::makeStatus(ErrorCode::AlreadyOpen);
            }

            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            Platform::DirectoryCursorOpenResult opened = Platform::openDirectoryCursor(path, options.symlinkPolicy);

            if(!opened.status.ok())
            {
                return std::move(opened.status);
            }

            // Publish the cursor state only after the platform open succeeds.
            options_ = options;
            emittedEntries_ = 0;
            limitReached_ = false;
            state_ = std::move(opened.state);
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

    bool DirectoryCursor::isOpen() const noexcept
    {
        return state_ != nullptr;
    }

    Types::Directory::CursorNextResult DirectoryCursor::next() noexcept
    {
        try
        {
            // Reject closed cursors and repeated calls after the entry limit.
            if(!state_)
            {
                return {.status = IO::makeStatus(ErrorCode::NotOpen)};
            }

            if(limitReached_)
            {
                return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
            }

            // Skip entries excluded by the caller's listing options.
            while(true)
            {
                Platform::DirectoryCursorNextResult next = Platform::readDirectoryCursor(*state_);

                if(!next.status.ok())
                {
                    return {.status = next.status};
                }

                if(!next.hasEntry)
                {
                    return {.status = IO::successStatus()};
                }

                if((!options_.includeHidden && next.hidden) || !includeEntryKind(next.entry.info.kind, options_))
                {
                    continue;
                }

                // Enforce the entry limit before returning the next visible item.
                if(emittedEntries_ >= options_.maxEntries)
                {
                    limitReached_ = true;
                    return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
                }

                ++emittedEntries_;
                return {.status = IO::successStatus(), .entry = std::move(next.entry), .hasEntry = true};
            }
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

    IO::Types::Status DirectoryCursor::close() noexcept
    {
        state_.reset();
        emittedEntries_ = 0;
        limitReached_ = false;
        return IO::successStatus();
    }

} // namespace GameWIP::FileSystem
