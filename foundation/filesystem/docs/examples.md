@page filesystem_examples Examples

The examples are complete translation units intended to show contract handling rather than application policy.

## Bounded whole-file read

```cpp
#include "filesystem/filesystem.h"

#include <cstdint>

GameWIP::IO::Types::Status loadSmallConfig()
{
    GameWIP::FileSystem::Types::File::ReadOptions options{};
    options.maxBytes = 1024U * 1024U;

    const auto result = GameWIP::FileSystem::readAllText("config.json", options);
    if (!result.status.ok())
    {
        // result.text can contain data collected before failure.
        return result.status;
    }

    return GameWIP::IO::successStatus();
}
```

## Streaming read with a final nonzero chunk

```cpp
#include "filesystem/filesystem.h"

#include <array>
#include <cstddef>
#include <span>

GameWIP::IO::Types::Status scanFile(const GameWIP::FileSystem::Types::Path& path)
{
    GameWIP::FileSystem::FileReader reader;
    auto status = reader.open(path);
    if (!status.ok())
    {
        return status;
    }

    std::array<std::byte, 8192> buffer{};
    while (true)
    {
        const auto result = reader.read(buffer);
        if (result.bytesRead != 0)
        {
            const std::span<const std::byte> chunk{buffer.data(), result.bytesRead};
            (void)chunk; // Consume the chunk before the next read.
        }

        if (!result.status.ok() || result.endOfStream)
        {
            status = result.status;
            break;
        }
    }

    const auto closeStatus = reader.close();
    return status.ok() ? closeStatus : status;
}
```

## Modify and resize an existing file

```cpp
#include "filesystem/filesystem.h"

#include <cstddef>
#include <span>
#include <string_view>

GameWIP::IO::Types::Status addMarker(const GameWIP::FileSystem::Types::Path& path)
{
    namespace FileSystem = GameWIP::FileSystem;
    namespace IO = GameWIP::IO;

    FileSystem::File file;
    FileSystem::Types::File::OpenOptions options{};
    options.access = FileSystem::Types::File::Access::ReadWrite;
    options.mode = FileSystem::Types::File::OpenMode::OpenExisting;
    options.initialPosition = FileSystem::Types::File::InitialPosition::End;

    auto status = file.open(path, options);
    if (!status.ok())
    {
        return status;
    }

    constexpr std::string_view marker = "done";
    const auto write = file.write(std::as_bytes(std::span{marker.data(), marker.size()}));
    if (!write.status.ok() || write.bytesWritten != marker.size())
    {
        const auto closeStatus = file.close();
        return write.status.ok() ? IO::makeStatus(IO::Types::ErrorCode::PartialWrite) : write.status;
    }

    status = file.flush(IO::Types::FlushMode::Data);
    if (status.ok())
    {
        status = file.close();
    }
    return status;
}
```

## Normal replacement and append

```cpp
#include "filesystem/filesystem.h"

GameWIP::IO::Types::Status writeSessionLog()
{
    namespace FileSystem = GameWIP::FileSystem;

    const auto replace = FileSystem::writeAllText("session.log", "start\n");
    if (!replace.status.ok())
    {
        return replace.status;
    }

    FileSystem::Types::File::AppendOptions options{};
    options.flushMode = GameWIP::IO::Types::FlushMode::Data;

    const auto append = FileSystem::appendText("session.log", "ready\n", options);
    return append.status;
}
```

## Atomic save replacement

```cpp
#include "filesystem/filesystem.h"

#include <string_view>

GameWIP::IO::Types::Status commitSave(std::string_view json)
{
    GameWIP::FileSystem::Types::File::AtomicWriteOptions options{};
    options.replaceMode = GameWIP::FileSystem::Types::ReplaceMode::ReplaceExisting;
    options.flushMode = GameWIP::IO::Types::FlushMode::Data;
    options.flushParentDirectory = true;

    return GameWIP::FileSystem::writeAllTextAtomic("saves/profile.json", json, options);
}
```

## Listing with partial-result handling

```cpp
#include "filesystem/filesystem.h"

#include <cstdint>

GameWIP::IO::Types::Status listAssets()
{
    GameWIP::FileSystem::Types::Directory::ListOptions options{};
    options.includeHidden = false;
    options.maxEntries = 1024;

    auto result = GameWIP::FileSystem::listDirectory("assets", options);
    for (const auto& entry : result.entries)
    {
        const auto& path = entry.path;
        const auto kind = entry.info.kind;
        (void)path;
        (void)kind;
    }

    return result.status;
}
```

## Metadata, copy, move, and removal

```cpp
#include "filesystem/filesystem.h"

GameWIP::IO::Types::Status rotateDataFile()
{
    namespace FileSystem = GameWIP::FileSystem;

    const auto size = FileSystem::getFileSize("data.bin");
    if (!size.status.ok())
    {
        return size.status;
    }

    FileSystem::Types::File::CopyOptions copy{};
    copy.replaceMode = FileSystem::Types::ReplaceMode::ReplaceExisting;
    copy.metadataMode = FileSystem::Types::File::CopyMetadataMode::Basic;
    copy.createParentDirectories = true;

    auto status = FileSystem::copyFile("data.bin", "backup/data.bin", copy);
    if (status.ok())
    {
        FileSystem::Types::MoveOptions move{};
        move.replaceMode = FileSystem::Types::ReplaceMode::ReplaceExisting;
        status = FileSystem::movePath("backup/data.bin", "backup/current.bin", move);
    }
    if (status.ok())
    {
        status = FileSystem::removeFile("backup/current.bin");
    }
    return status;
}
```

## UTF-8 path boundary and lexical operations

```cpp
#include "filesystem/filesystem.h"

GameWIP::IO::Types::Status buildSavePath()
{
    namespace FileSystem = GameWIP::FileSystem;

    const auto leaf = FileSystem::pathFromUtf8("slot-\xE2\x98\x85.json");
    if (!leaf.status.ok())
    {
        return leaf.status;
    }

    const auto joined = FileSystem::joinPath("saves", leaf.path);
    if (!joined.status.ok())
    {
        return joined.status;
    }

    const auto text = FileSystem::pathToUtf8(joined.path);
    return text.status;
}
```

## Whole-file lock ownership

Because `FileLock` deliberately deletes move assignment, APIs that transfer a lock should return it or construct the destination directly. A practical ownership pattern is:

```cpp
#include "filesystem/filesystem.h"

GameWIP::FileSystem::Types::Lock::Result openAndLock(
    const GameWIP::FileSystem::Types::Path& path,
    GameWIP::FileSystem::File& owner)
{
    const auto status = owner.open(path);
    if (!status.ok())
    {
        return {.status = status};
    }
    return owner.tryLockExclusive();
}
```

The returned `FileLock` remains responsible for unlocking. Explicit `owner.close()` reports `ResourceBusy` until the lock is released.

## Process current directory

```cpp
#include "filesystem/filesystem.h"

GameWIP::IO::Types::Status temporarilySelectDirectory(
    const GameWIP::FileSystem::Types::Path& directory)
{
    const auto previous = GameWIP::FileSystem::getCurrentDirectory();
    if (!previous.status.ok())
    {
        return previous.status;
    }

    auto status = GameWIP::FileSystem::setCurrentDirectory(directory);
    if (!status.ok())
    {
        return status;
    }

    // All threads now resolve relative paths from directory.

    const auto restore = GameWIP::FileSystem::setCurrentDirectory(previous.path);
    return restore;
}
```

Avoid this pattern in concurrent application code unless process-wide coordination is explicit.
