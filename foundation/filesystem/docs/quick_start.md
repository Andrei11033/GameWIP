@page foundation_filesystem_quick_start FileSystem quick start

Include `filesystem/filesystem.h` and link `FileSystem`.

Examples use this namespace alias:

```cpp
namespace FileSystem = GameWIP::FileSystem;
namespace IO = GameWIP::IO;
```

## Whole-file text

Use the whole-file helpers when the operation is one complete read, write, or append.

```cpp
const auto write = FileSystem::writeAllText("config/player.cfg", "volume=80\n");
if (!write.status.ok()) {
    return write.status;
}

const auto read = FileSystem::readAllText("config/player.cfg");
if (!read.status.ok()) {
    return read.status;
}
```

Text helpers preserve UTF-8 bytes. They do not add a BOM, remove a BOM, validate encoding, or translate line endings.

## Explicit handles

Use `FileReader`, `FileWriter`, or `File` when the caller needs repeated transfers, seeking, sharing policy, locking, or a controlled handle lifetime.

```cpp
FileSystem::FileReader reader;
IO::Types::Status status = reader.open("data/world.bin");
if (!status.ok()) {
    return status;
}

std::array<std::byte, 4096> scratch{};
IO::Types::ReadResult read = reader.read(std::span<std::byte>(scratch.data(), scratch.size()));
status = reader.close();
```

Close explicitly when the caller needs to observe close or flush failure. Destructors clean up on a best-effort basis and never throw.

## Directories and metadata

```cpp
IO::Types::Status create = FileSystem::createDirectories("saves/profile1");
auto entries = FileSystem::listDirectory("saves");
auto info = FileSystem::getEntryInfo("saves/profile1");
```

`listDirectory()` returns direct children only. Recursive traversal belongs in caller code unless the operation is `removeDirectoryTree()`.

## Atomic replacement

Use atomic writes for exact file replacement where readers should see either old content or complete new content.

```cpp
FileSystem::Types::AtomicWriteOptions options{};
options.replaceMode = FileSystem::Types::ReplaceMode::ReplaceExisting;
options.flushMode = IO::Types::FlushMode::Data;

IO::Types::Status status = FileSystem::writeAllTextAtomic("saves/profile1/save.json", jsonText, options);
```

There is no non-atomic fallback. A failure before commit leaves an existing destination unchanged.

## Default symlink policy

Public operations that expose a symlink policy default to `SymlinkPolicy::DoNotFollow`. Choose `FollowFinal` or `FollowAll` only when traversal through symlinks is intentional.

See @ref foundation_filesystem_public_api and @ref foundation_filesystem_examples for broader API coverage.
