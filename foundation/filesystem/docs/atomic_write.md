@page foundation_filesystem_atomic_write FileSystem Atomic Write

This page documents planned whole-file replacement behavior for `GameWIP::FileSystem`.

No atomic-write behavior is implemented in this pass.

## Planned types and API

```cpp
namespace GameWIP::FileSystem::Types {

struct AtomicWriteOptions {
    bool createParentDirectories = true;
    bool overwriteExisting = true;
    IO::Types::FlushMode flushMode = IO::Types::FlushMode::Data;
    std::string temporarySuffix = ".tmp";
};

} // namespace GameWIP::FileSystem::Types

namespace GameWIP::FileSystem {

[[nodiscard]] IO::Types::Status writeFileAtomic(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    const Types::AtomicWriteOptions& options = {});

[[nodiscard]] IO::Types::Status writeTextFileAtomic(
    const std::filesystem::path& path,
    std::string_view utf8Text,
    const Types::AtomicWriteOptions& options = {});

} // namespace GameWIP::FileSystem
```

## Intended use

Atomic write APIs are intended for future saves, configs, controls, and other whole-file replacement cases where a half-written final file would be worse than leaving the previous file in place.

## Contract

The planned behavior is:

- write the new bytes to a temporary path near the final path;
- flush according to `flushMode`;
- replace the final path only after temporary-file write succeeds;
- report replacement failures through `IO::Types::Status`;
- preserve the old final file on forced failure where practical.

Atomic write means avoiding a half-written final file during normal replacement behavior.

It does not promise database-style transactions, multi-file atomicity, or perfect power-loss durability.

## Text

`writeTextFileAtomic()` treats public text as UTF-8 bytes. It does not parse JSON, config, controls, save, or asset data.
