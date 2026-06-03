@page foundation_filesystem_directory_operations FileSystem Directory Operations

This page documents planned directory and path operation contracts for `GameWIP::FileSystem`.

No directory behavior is implemented in this pass.

## Planned types

```cpp
namespace GameWIP::FileSystem::Types {

struct CreateDirectoryOptions {
    bool succeedIfAlreadyExists = true;
};

struct ListDirectoryOptions {
    bool includeFiles = true;
    bool includeDirectories = true;
    bool includeSymlinks = true;
    bool includeOther = true;
    bool includeHidden = true;
    std::uint64_t maxEntries = IO::kNoByteLimit;
};

struct DirectoryEntry {
    std::filesystem::path path;
    FileStat stat;
};

struct ListDirectoryResult {
    IO::Types::Status status;
    std::vector<DirectoryEntry> entries;
    bool wasTruncated = false;
};

struct RemoveOptions {
    bool succeedIfMissing = false;
};

struct RemoveTreeOptions {
    bool succeedIfMissing = false;
    std::uint64_t maxEntries = IO::kNoByteLimit;
};

struct RemoveTreeResult {
    IO::Types::Status status;
    std::uint64_t removedEntries = 0;
};

struct MoveOptions {
    bool replaceExisting = false;
    bool createParentDirectories = false;
};

struct CopyFileOptions {
    bool replaceExisting = false;
    bool createParentDirectories = false;
};

} // namespace GameWIP::FileSystem::Types
```

## Planned operations

```cpp
[[nodiscard]] IO::Types::Status createDirectory(
    const std::filesystem::path& path,
    const Types::CreateDirectoryOptions& options = {});

[[nodiscard]] IO::Types::Status createDirectories(
    const std::filesystem::path& path,
    const Types::CreateDirectoryOptions& options = {});

[[nodiscard]] Types::ExistsResult exists(
    const std::filesystem::path& path);

[[nodiscard]] Types::StatResult stat(
    const std::filesystem::path& path);

[[nodiscard]] Types::ListDirectoryResult listDirectory(
    const std::filesystem::path& path,
    const Types::ListDirectoryOptions& options = {});

[[nodiscard]] IO::Types::Status removeFile(
    const std::filesystem::path& path,
    const Types::RemoveOptions& options = {});

[[nodiscard]] IO::Types::Status removeEmptyDirectory(
    const std::filesystem::path& path,
    const Types::RemoveOptions& options = {});

[[nodiscard]] Types::RemoveTreeResult removeDirectoryTree(
    const std::filesystem::path& path,
    const Types::RemoveTreeOptions& options = {});

[[nodiscard]] IO::Types::Status movePath(
    const std::filesystem::path& from,
    const std::filesystem::path& to,
    const Types::MoveOptions& options = {});

[[nodiscard]] IO::Types::Status copyFile(
    const std::filesystem::path& from,
    const std::filesystem::path& to,
    const Types::CopyFileOptions& options = {});
```

## Contract notes

`listDirectory()` is non-recursive in v1.

`removeDirectoryTree()` must not recursively follow symlinked directories.

`maxEntries` gives tests and callers a way to bound directory traversal work.

Move, copy, and remove operations report expected failures through `IO::Types::Status`.
