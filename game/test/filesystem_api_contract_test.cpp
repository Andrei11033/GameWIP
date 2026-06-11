/// @file filesystem_api_contract_test.cpp
/// @brief Compile-time checks for the declaration-only FileSystem public API.

#include "filesystem/filesystem.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
    namespace FileSystem = GameWIP::FileSystem;
    namespace IO = GameWIP::IO;

    using Path = FileSystem::Types::Path;
    using ConstByteSpan = std::span<const std::byte>;

    static_assert(
        std::is_same_v<decltype(FileSystem::writeAllBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::writeAllText(std::declval<const Path &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::appendBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::appendText(std::declval<const Path &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::writeAllBytesAtomic(std::declval<const Path &>(), std::declval<ConstByteSpan>())), IO::Types::Status>);

    static_assert(noexcept(FileSystem::writeAllBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())));
    static_assert(noexcept(FileSystem::appendBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())));
    static_assert(noexcept(FileSystem::resizeFile(std::declval<const Path &>(), 0)));
    static_assert(noexcept(FileSystem::truncateFile(std::declval<const Path &>())));

    constexpr FileSystem::Types::FileShare kReadDelete = FileSystem::Types::FileShare::Read | FileSystem::Types::FileShare::Delete;
    static_assert((kReadDelete & FileSystem::Types::FileShare::Read) == FileSystem::Types::FileShare::Read);
    static_assert((kReadDelete & FileSystem::Types::FileShare::Write) == FileSystem::Types::FileShare::None);

    static_assert(FileSystem::Types::FileOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::FileReaderOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::FileWriterOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::WriteFileOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::AppendFileOptions{}.share == FileSystem::Types::FileShare::All);

    static_assert(FileSystem::Types::FileOpenOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::FollowAll);
    static_assert(FileSystem::Types::AtomicWriteOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::FollowAll);
    static_assert(FileSystem::Types::MutationOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::FollowAll);
    static_assert(FileSystem::Types::QueryOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::RemoveOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);

    static_assert(std::is_move_constructible_v<FileSystem::File>);
    static_assert(!std::is_move_assignable_v<FileSystem::File>);
    static_assert(std::is_move_constructible_v<FileSystem::FileReader>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileReader>);
    static_assert(std::is_move_constructible_v<FileSystem::FileWriter>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileWriter>);
    static_assert(std::is_move_constructible_v<FileSystem::FileLock>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileLock>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::AtomicWriteOptions{}.temporaryNamePrefix), std::string>);
} // namespace
