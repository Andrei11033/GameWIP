@page filesystem_quick_start Quick start

The first workflow writes and reads a complete UTF-8 file with checked results.
It also shows the package dependencies and status handling every FileSystem
consumer needs.

## Include

```cpp
#include "filesystem/filesystem.h"
```

`filesystem/filesystem.h` is the complete convenience include. Consumers may
instead include `filesystem/path.h`, `filesystem/entry.h`,
`filesystem/file.h`, or `filesystem/directory.h` for the concepts they use.
The selected valid header does not change API behavior.

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(FileSystem ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::FileSystem)
```

The package resolves the exact-version IO and Unicode dependencies. IO is FileSystem's public API dependency; Unicode supports FileSystem-owned text
boundaries.

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE FileSystem)
```

## Minimal usage

```cpp
#include "filesystem/filesystem.h"

namespace FileSystem = GameWIP::FileSystem;

GameWIP::IO::Types::Status saveAndLoadSettings()
{
    const auto write = FileSystem::writeAllText("config/player.cfg", "volume=80\n");
    if (!write.status.ok())
    {
        return write.status;
    }

    const auto read = FileSystem::readAllText("config/player.cfg");
    if (!read.status.ok())
    {
        return read.status;
    }

    return GameWIP::IO::successStatus();
}
```

Text helpers enforce strict UTF-8. They do not normalize text, add or remove a BOM, or translate line endings. Use the byte helpers for arbitrary
content.

## Explicit handle

```cpp
#include "filesystem/filesystem.h"

#include <array>
#include <cstddef>
#include <span>

GameWIP::IO::Types::Status inspectPrefix(const GameWIP::FileSystem::Types::Path& path)
{
    namespace FileSystem = GameWIP::FileSystem;

    FileSystem::FileReader reader;
    auto status = reader.open(path);
    if (!status.ok())
    {
        return status;
    }

    std::array<std::byte, 4096> scratch{};
    const auto read = reader.read(scratch);
    if (!read.status.ok())
    {
        const auto closeStatus = reader.close();
        return read.status;
    }

    // Process scratch[0..read.bytesRead). A final read may contain bytes and set endOfStream.

    status = reader.close();
    return status;
}
```

Close explicitly when close or configured flush-on-close failure must be observed. Destructors perform best-effort cleanup and cannot report failure.

## Atomic replacement

```cpp
#include "filesystem/filesystem.h"

GameWIP::IO::Types::Status replaceSave(std::string_view json)
{
    GameWIP::FileSystem::Types::File::AtomicWriteOptions options{};
    options.flushMode = GameWIP::IO::Types::FlushMode::Data;
    options.flushParentDirectory = true;

    return GameWIP::FileSystem::writeAllTextAtomic("saves/profile.json", json, options);
}
```

There is no non-atomic fallback. Failure before commit preserves an existing destination. A late parent-directory flush failure can be returned after
the replacement is already visible.

## Failure handling

Always inspect the status before consuming a result as complete. Some failures intentionally preserve progress:

- whole-file reads may return collected bytes or text with a failed status;
- non-atomic writes may return a nonzero `bytesWritten` with failure;
- directory listing may return collected entries with `SizeLimitExceeded` or another failure;
- tree removal may report entries already removed before failure;
- a failed `close()` or `unlock()` leaves the resource active so the operation can be retried.

`Types::Lock::Outcome::WouldBlock` is a successful non-acquisition result, not a failed status.

## Where to go next

- @ref filesystem_public_api maps the complete public surface.
- @ref filesystem_whole_file_io explains whole-file transfer and progress.
- @ref filesystem_file_open_modes covers handles, sharing, locking, and close behavior.
- @ref filesystem_symlink_policies explains traversal policy by operation family.
- @ref filesystem_atomic_write covers replacement atomicity and durability.
- @ref filesystem_examples provides complete integration examples.
