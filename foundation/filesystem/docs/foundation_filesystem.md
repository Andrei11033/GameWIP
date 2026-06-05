@page foundation_filesystem FileSystem

`GameWIP::FileSystem` is the planned portable public API for local filesystem operating-system calls.

This page documents the intended public API only. No FileSystem behavior or backend is implemented in this pass.

Doxygen registration and the library target are still to be implemented. Until then, these pages are planning material rather than generated library manual pages.

## Documentation sections

- @subpage foundation_filesystem_file_open_modes
- @subpage foundation_filesystem_atomic_write
- @subpage foundation_filesystem_directory_operations
- @subpage foundation_filesystem_unicode_paths

## Purpose

`GameWIP::FileSystem` owns local file and directory access. It depends on `GameWIP::IO` for shared status, result, flush, and byte/text helper concepts.

It is for local files and directories only.

## Namespace

```cpp
namespace GameWIP::FileSystem;
namespace GameWIP::FileSystem::Types;
```

Value types live under `GameWIP::FileSystem::Types`. Active APIs live directly under `GameWIP::FileSystem`.

## Planned active API

```cpp
namespace GameWIP::FileSystem {

class File;
class FileReader;
class FileWriter;

namespace Types {
struct OpenFileResult;
struct OpenReaderResult;
struct OpenWriterResult;
}

[[nodiscard]] Types::OpenFileResult openFile(
    const std::filesystem::path& path,
    const Types::OpenFileOptions& options = {});

[[nodiscard]] Types::OpenReaderResult openReader(
    const std::filesystem::path& path,
    const Types::OpenReaderOptions& options = {});

[[nodiscard]] Types::OpenWriterResult openWriter(
    const std::filesystem::path& path,
    const Types::OpenWriterOptions& options = {});

[[nodiscard]] Types::ReadBytesResult readAllBytes(
    const std::filesystem::path& path,
    const Types::ReadFileOptions& options = {});

[[nodiscard]] Types::ReadTextResult readAllText(
    const std::filesystem::path& path,
    const Types::ReadFileOptions& options = {});

[[nodiscard]] IO::Types::Status writeAllBytes(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    const Types::WriteFileOptions& options = {});

[[nodiscard]] IO::Types::Status writeAllText(
    const std::filesystem::path& path,
    std::string_view utf8Text,
    const Types::WriteFileOptions& options = {});

[[nodiscard]] IO::Types::Status appendBytes(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    const Types::AppendFileOptions& options = {});

[[nodiscard]] IO::Types::Status appendText(
    const std::filesystem::path& path,
    std::string_view utf8Text,
    const Types::AppendFileOptions& options = {});

[[nodiscard]] IO::Types::Status writeFileAtomic(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    const Types::AtomicWriteOptions& options = {});

[[nodiscard]] IO::Types::Status writeTextFileAtomic(
    const std::filesystem::path& path,
    std::string_view utf8Text,
    const Types::AtomicWriteOptions& options = {});

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

} // namespace GameWIP::FileSystem
```

## Planned general passive types

```cpp
namespace GameWIP::FileSystem::Types {

enum class FileKind {
    Missing,
    RegularFile,
    Directory,
    Symlink,
    Other
};

struct FileStat {
    FileKind kind = FileKind::Missing;
    std::uint64_t sizeBytes = 0;
    std::chrono::system_clock::time_point lastWriteTime {};
    bool readOnly = false;
};

struct StatResult {
    IO::Types::Status status;
    FileStat stat;
};

struct ExistsResult {
    IO::Types::Status status;
    bool exists = false;
};

struct ReadBytesResult {
    IO::Types::Status status;
    std::vector<std::byte> bytes;
};

struct ReadTextResult {
    IO::Types::Status status;
    std::string text;
};

} // namespace GameWIP::FileSystem::Types
```

## Non-goals

`GameWIP::FileSystem` does not parse JSON, controls, config, save, asset package, or texture formats.

It does not provide network I/O, async I/O, memory-mapped files, file watching, terminal input, or terminal output.

## Notes

`writeFileAtomic()` and `writeTextFileAtomic()` are intended to support future saves, configs, and controls files. The FileSystem layer owns the local file replacement behavior, not the higher-level file format.
