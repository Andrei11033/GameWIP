@page filesystem_examples FileSystem examples

These examples use the shared `GameWIP::Status::Types::Result<T>` contract:
`.ok()` reports completion, `.status()` gives the operation outcome, and
`.value()` contains a complete or partial value when `.hasValue()` is true.

## Read and write a text file

```cpp
GameWIP::Status::Types::Result<void> saveAndLoad()
{
    namespace FileSystem = GameWIP::FileSystem;

    const auto write = FileSystem::writeAllText("config/player.cfg", "volume=80\n");
    if(!write.ok())
    {
        return GameWIP::Status::Types::Result<void>::failure(write.status());
    }

    const auto read = FileSystem::readAllText("config/player.cfg");
    if(!read.ok())
    {
        return GameWIP::Status::Types::Result<void>::failure(read.status());
    }

    // Use read.value().
    return GameWIP::Status::Types::Result<void>::success();
}
```

## Preserve partial progress

```cpp
auto result = GameWIP::FileSystem::readAllBytes("data/save.bin");
if(!result.ok())
{
    if(result.hasValue())
    {
        processPartialBytes(result.value());
    }

    report(result.status().code());
    return;
}

processBytes(result.value());
```

## Stream a file

```cpp
GameWIP::Status::Types::Result<void> inspectPrefix(const GameWIP::FileSystem::Types::Path &path)
{
    namespace FileSystem = GameWIP::FileSystem;

    FileSystem::FileReader reader;
    if(auto opened = reader.open(path); !opened.ok())
    {
        return GameWIP::Status::Types::Result<void>::failure(opened.status());
    }

    std::array<std::byte, 4096> scratch{};
    const auto read = reader.read(scratch);
    if(!read.ok())
    {
        static_cast<void>(reader.close());
        return GameWIP::Status::Types::Result<void>::failure(read.status());
    }

    processBytes(std::span{scratch}.first(read.value().bytesRead));
    return reader.close();
}
```

## List a directory

```cpp
auto listing = GameWIP::FileSystem::listDirectory("assets");
if(listing.hasValue())
{
    for(const auto &entry : listing.value())
    {
        process(entry.path, entry.info);
    }
}

if(!listing.ok())
{
    report(listing.status().code());
}
```

## Replace a file atomically

```cpp
GameWIP::Status::Types::Result<void> replaceSave(std::string_view json)
{
    GameWIP::FileSystem::Types::File::AtomicWriteOptions options{};
    options.flushMode = GameWIP::IO::Types::FlushMode::Data;
    options.flushParentDirectory = true;

    return GameWIP::FileSystem::writeAllTextAtomic("saves/profile.json", json, options);
}
```

## Acquire a lock

```cpp
GameWIP::Status::Types::Result<GameWIP::FileSystem::FileLock> lockSave(GameWIP::FileSystem::File &file)
{
    auto lock = file.tryLockExclusive();
    if(!lock.ok())
    {
        // WouldBlock is reported by status().code().
        return GameWIP::Status::Types::Result<GameWIP::FileSystem::FileLock>::failure(lock.status());
    }

    return GameWIP::Status::Types::Result<GameWIP::FileSystem::FileLock>::success(std::move(lock).value());
}
```

## Build a path from UTF-8

```cpp
auto name = GameWIP::FileSystem::pathFromUtf8("saves/profile.json");
if(!name.ok())
{
    report(name.status().code());
    return;
}

auto path = GameWIP::FileSystem::joinPath("data", name.value());
if(path.ok())
{
    openFile(path.value());
}
```

## Change the process directory

```cpp
auto previous = GameWIP::FileSystem::getCurrentDirectory();
if(!previous.ok())
{
    report(previous.status().code());
    return;
}

if(auto changed = GameWIP::FileSystem::setCurrentDirectory("data"); !changed.ok())
{
    report(changed.status().code());
    return;
}

static_cast<void>(GameWIP::FileSystem::setCurrentDirectory(previous.value()));
```

## Related pages

- @ref filesystem_quick_start
- @ref filesystem_public_api
- @ref filesystem_whole_file_io
- @ref filesystem_file_open_modes
- @ref filesystem_directory_operations
- @ref filesystem_atomic_write
