@page filesystem_examples FileSystem examples

Examples use this namespace alias:

```cpp
namespace FileSystem = GameWIP::FileSystem;
namespace IO = GameWIP::IO;
```

## Read and write text

```cpp
IO::Types::WriteResult write = FileSystem::writeAllText("settings.ini", "fullscreen=true\n");
if (!write.status.ok())
{
    return write.status;
}

IO::Types::ReadAllTextResult read = FileSystem::readAllText("settings.ini");
if (!read.status.ok())
{
    return read.status;
}
```

## Append a line

```cpp
FileSystem::Types::AppendFileOptions options{};
options.flushMode = IO::Types::FlushMode::Data;

IO::Types::WriteResult append = FileSystem::appendText("logs/session.txt", "loaded save\n", options);
```

## Stream bytes with a reader

```cpp
FileSystem::FileReader reader;
IO::Types::Status status = reader.open("data/package.bin");
if (!status.ok())
{
    return status;
}

std::array<std::byte, 8192> scratch{};
while (true)
{
    IO::Types::ReadResult read = reader.read(std::span<std::byte>(scratch.data(), scratch.size()));
    if (!read.status.ok() || read.endOfStream)
    {
        status = read.status;
        break;
    }

    consumeBytes(std::span<const std::byte>(scratch.data(), read.bytesRead));
}

IO::Types::Status close = reader.close();
```

## Modify an existing file

```cpp
FileSystem::File file;
IO::Types::Status status = file.open(
    "save.bin",
    FileSystem::Types::FileOpenOptions{
        .access = FileSystem::Types::FileAccess::ReadWrite,
        .mode = FileSystem::Types::FileOpenMode::OpenExisting});

if (status.ok())
{
    status = file.seek(0, IO::Types::SeekOrigin::End);
}
if (status.ok())
{
    const std::string marker = "done";
    status = file.write(std::as_bytes(std::span<const char>(marker.data(), marker.size()))).status;
}
if (status.ok())
{
    status = file.close();
}
```

`FileInitialPosition::End` performs one initial seek. Use append modes when every write must target the then-current end of file.

## Atomic save replacement

```cpp
FileSystem::Types::AtomicWriteOptions options{};
options.replaceMode = FileSystem::Types::ReplaceMode::ReplaceExisting;
options.flushMode = IO::Types::FlushMode::Data;
options.flushParentDirectory = true;

IO::Types::Status status = FileSystem::writeAllTextAtomic("saves/profile.json", jsonText, options);
```

Before commit, failure leaves an existing destination unchanged. After commit, the path names the complete replacement.

## List direct children

```cpp
FileSystem::Types::ListDirectoryOptions options{};
options.includeHidden = false;
options.maxEntries = 1024;

FileSystem::Types::ListDirectoryResult listing = FileSystem::listDirectory("assets", options);
for (const FileSystem::Types::DirectoryEntry& entry : listing.entries)
{
    useEntry(entry.path, entry.info);
}
```

If `maxEntries` is reached, the status is `SizeLimitExceeded` and the entries collected so far remain available.

## Query and mutate metadata

```cpp
auto size = FileSystem::getFileSize("save.bin");
auto info = FileSystem::getEntryInfo("save.bin");

IO::Types::Status readOnly = FileSystem::setReadOnly("save.bin", true);
IO::Types::Status writable = FileSystem::setReadOnly("save.bin", false);
```

Predicate helpers such as `exists()` and `isDirectory()` return successful `false` for missing paths. Value queries report `NotFound`.

## Copy, move, and remove

```cpp
FileSystem::Types::CopyFileOptions copyOptions{};
copyOptions.metadataMode = FileSystem::Types::CopyMetadataMode::Basic;

IO::Types::Status status = FileSystem::copyFile("source.dat", "backup/source.dat", copyOptions);
if (status.ok())
{
    status = FileSystem::movePath("backup/source.dat", "backup/current.dat");
}
if (status.ok())
{
    status = FileSystem::removeFile("backup/current.dat");
}
```

`movePath()` is a native move or rename. It does not perform a cross-volume copy/delete fallback.

## UTF-8 path boundary

```cpp
auto path = FileSystem::pathFromUtf8("saves/slot-\xE2\x98\x85.json");
if (!path.status.ok())
{
    return path.status;
}

auto utf8 = FileSystem::pathToUtf8(path.path.filename());
```

Use these helpers where external text is explicitly UTF-8.

## Whole-file lock

```cpp
FileSystem::File file;
IO::Types::Status status = file.open("save.bin");
if (!status.ok())
{
    return status;
}

auto lock = file.tryLockExclusive();
if (lock.status.ok() && lock.outcome == FileSystem::Types::LockOutcome::Acquired)
{
    updateFile(file);
    status = lock.lock.unlock();
}

if (status.ok())
{
    status = file.close();
}
```

Lock attempts are non-blocking. `WouldBlock` is reported through `LockResult::outcome`, not as a failure status.
