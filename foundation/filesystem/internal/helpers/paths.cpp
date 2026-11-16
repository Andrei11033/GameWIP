/// @file paths.cpp
/// @brief Shared FileSystem path and entry helpers.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/filesystem_platform.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <string>

namespace GameWIP::FileSystem::Detail
{
    /// ------------------------------------------------------------
    /// UTF-8 path conversion
    /// ------------------------------------------------------------

    bool validUtf8(std::string_view text) noexcept
    {
        return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
    }

    Types::Path pathFromTrustedUtf8(std::string_view utf8Path)
    {
        std::u8string converted(utf8Path.size(), u8'\0');

        if(!utf8Path.empty())
        {
            std::ranges::transform(
                utf8Path,
                converted.begin(),
                [](char byte)
                {
                    return static_cast<char8_t>(byte);
                });
        }

        return Types::Path{converted};
    }

    /// ------------------------------------------------------------
    /// Validated entry queries
    /// ------------------------------------------------------------

    Platform::EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
    {
        if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        return Platform::queryEntry(path, symlinkPolicy);
    }

    bool finalEntryIsSymlink(const Types::Path &path) noexcept
    {
        const Platform::EntryQueryResult result = queryEntry(path, Types::SymlinkPolicy::DoNotFollow);
        return result.status.ok() && result.info.kind == Types::EntryKind::Symlink;
    }
} // namespace GameWIP::FileSystem::Detail
