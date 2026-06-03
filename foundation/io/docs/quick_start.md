@page foundation_io_quick_start IO quick start

Include the public header and link `GameWIP::IO`.

```cpp
#include "io/io.h"

#include <cstddef>
#include <vector>

std::vector<std::byte> bytes{
    std::byte{0x61},
    std::byte{0x62},
    std::byte{0x63}};

GameWIP::IO::MemoryReader reader(bytes);
GameWIP::IO::Types::ReadAllBytesResult result = GameWIP::IO::readAllBytes(reader);
```

Use `Status::ok()` for expected failures:

```cpp
if (!result.status.ok()) {
    // Inspect result.status.code, GameWIP::IO::errorCodeName(result.status.code),
    // result.status.nativeCode, and result.status.message.
}
```

For text helpers, strings are treated as UTF-8 bytes. The IO layer does not validate or parse higher-level file formats.

```cpp
GameWIP::IO::MemoryReader textReader("jump=Space");
GameWIP::IO::Types::ReadAllTextResult text = GameWIP::IO::readAllText(textReader);
```

Collect output with `MemoryWriter`:

```cpp
GameWIP::IO::MemoryWriter writer;
GameWIP::IO::Types::Status writeStatus =
    GameWIP::IO::writeAllText(writer, "hello");

if (writeStatus.ok()) {
    std::string collected = writer.text();
    std::vector<std::byte> movedBytes = writer.takeBytes();
}
```

`MemoryReader` is non-owning. Keep its source bytes alive and at a stable address while the reader is used. Direct construction from temporary `std::string` and vector storage is rejected.

Use a finite `maxBytes` when reading untrusted or externally controlled streams. The limit is a hard accepted-size limit, not a truncation request.
