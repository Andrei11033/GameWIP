/// @file filesystem_path_header.cpp
/// @brief Verifies that the FileSystem path header is self-contained.

#include "filesystem/path.h"

namespace
{
    [[maybe_unused]] void usePathBooleanResult(const GameWIP::FileSystem::Types::Path &path)
    {
        const GameWIP::Status::Types::Result<bool> result = GameWIP::FileSystem::isAbsolutePath(path);
        (void)result;
    }
} // namespace
