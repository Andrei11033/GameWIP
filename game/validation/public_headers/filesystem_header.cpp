/// @file filesystem_header.cpp
/// @brief FileSystem public-header self-containment compile check.
///
/// This translation unit intentionally includes only `filesystem/filesystem.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "filesystem/filesystem.h"

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace FileSystem = GameWIP::FileSystem;

    static_assert(noexcept(std::declval<FileSystem::FileReader &>().read(std::declval<std::span<std::byte>>())));
    static_assert(noexcept(std::declval<FileSystem::FileReader &>().close()));
    static_assert(noexcept(std::declval<FileSystem::FileWriter &>().write(std::declval<std::span<const std::byte>>())));
    static_assert(noexcept(std::declval<FileSystem::FileWriter &>().flush()));
    static_assert(noexcept(std::declval<FileSystem::File &>().position()));
    static_assert(noexcept(std::declval<FileSystem::File &>().size()));
    static_assert(noexcept(std::declval<FileSystem::File &>().seek(0, GameWIP::IO::Types::SeekOrigin::Begin)));
    static_assert(noexcept(std::declval<FileSystem::File &>().resize(0)));

    static_assert(std::is_same_v<decltype(FileSystem::Types::File::OpenOptions{}.share), FileSystem::Types::File::Share>);
    static_assert(
        std::is_same_v<decltype(FileSystem::Types::Directory::ListResult{}.entries), std::vector<FileSystem::Types::Directory::Entry>>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::Lock::Result{}.outcome), FileSystem::Types::Lock::Outcome>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::EntryOptions{}.symlinkPolicy), FileSystem::Types::SymlinkPolicy>);
} // namespace
