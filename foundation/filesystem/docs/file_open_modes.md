@page foundation_filesystem_file_open_modes FileSystem Open Modes

This page documents planned FileSystem open-mode options.

No FileSystem behavior is implemented in this pass.

## Planned types

```cpp
namespace GameWIP::FileSystem::Types {

struct FileShareOptions {
    bool allowRead = true;
    bool allowWrite = false;
    bool allowDelete = false;
};

enum class FileAccess {
    Read,
    Write,
    ReadWrite
};

enum class FileCreateMode {
    OpenExisting,
    CreateNew,
    OpenOrCreate,
    TruncateExisting,
    CreateOrTruncate
};

enum class FileInitialPosition {
    Beginning,
    End
};

struct OpenFileOptions {
    FileAccess access = FileAccess::Read;
    FileCreateMode createMode = FileCreateMode::OpenExisting;
    FileInitialPosition initialPosition = FileInitialPosition::Beginning;

    FileShareOptions sharing {
        .allowRead = true,
        .allowWrite = false,
        .allowDelete = false
    };

    bool createParentDirectories = false;
};

struct OpenReaderOptions {
    FileShareOptions sharing {
        .allowRead = true,
        .allowWrite = true,
        .allowDelete = false
    };
};

enum class WriteMode {
    CreateNew,
    CreateOrTruncate,
    OpenExistingTruncate,
    OpenOrCreate,
    AppendOrCreate,
    AppendExisting
};

struct OpenWriterOptions {
    WriteMode mode = WriteMode::CreateOrTruncate;

    FileShareOptions sharing {
        .allowRead = true,
        .allowWrite = false,
        .allowDelete = false
    };

    bool createParentDirectories = false;
};

struct ReadFileOptions {
    OpenReaderOptions open;
    std::uint64_t maxBytes = IO::kNoByteLimit;
};

struct WriteFileOptions {
    WriteMode mode = WriteMode::CreateOrTruncate;

    FileShareOptions sharing {
        .allowRead = true,
        .allowWrite = false,
        .allowDelete = false
    };

    bool createParentDirectories = true;
    IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
};

struct AppendFileOptions {
    FileShareOptions sharing {
        .allowRead = true,
        .allowWrite = false,
        .allowDelete = false
    };

    bool createParentDirectories = true;
    IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
};

} // namespace GameWIP::FileSystem::Types
```

## Contract notes

`FileAccess` describes the requested access to a file object.

`FileCreateMode` describes whether an existing file is required, a new file is required, or truncation is allowed.

`WriteMode` is the higher-level writer convenience mode used by writer and whole-file APIs.

`FileShareOptions` are public, portable intent. The backend maps them to platform-specific sharing behavior where available.

`createParentDirectories` requests parent directory creation before opening or writing. Failure to create parents must be reported through `IO::Types::Status`.
