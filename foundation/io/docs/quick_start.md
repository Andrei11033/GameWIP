@page io_quick_start Quick start

## Include

```cpp
#include "io/io.h"
```

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(IO ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::IO)
```

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE IO)
```

## Minimal usage

The following example reads stable caller-owned text with a hard size limit, then writes a result to memory:

```cpp
#include "io/io.h"

#include <string>

int main()
{
    std::string source = "jump=Space";
    GameWIP::IO::MemoryReader reader(source);

    constexpr std::uint64_t maximumConfigBytes = 16 * 1024;
    GameWIP::IO::Types::ReadAllTextResult readResult =
        GameWIP::IO::readAllText(reader, maximumConfigBytes);

    if (!readResult.status.ok())
    {
        // readResult.text may contain bytes produced before the failure.
        return 1;
    }

    GameWIP::IO::MemoryWriter writer;
    GameWIP::IO::Types::WriteResult writeResult =
        GameWIP::IO::writeAllText(writer, readResult.text);

    if (!writeResult.status.ok())
    {
        // writeResult.bytesWritten reports total accepted progress.
        return 1;
    }

    const std::string copy = writer.text();
    return copy == source ? 0 : 1;
}
```

`MemoryReader` is non-owning. Its source must remain alive and at a stable address until the reader is no longer used. Direct construction from temporary `std::string` and `std::vector<std::byte>` objects is rejected, but caller-created dangling spans and string views cannot be detected.

## Failure handling

Expected I/O failures are returned through `Types::Status`:

```cpp
#include "io/io.h"

#include <cstdint>
#include <string>
#include <string_view>

void inspectFailure(const GameWIP::IO::Types::Status& status)
{
    if (status.ok())
    {
        return;
    }

    const GameWIP::IO::Types::ErrorCode code = status.code;
    const std::string_view name = GameWIP::IO::errorCodeName(code);
    const std::int64_t nativeCode = status.nativeCode;
    const std::string& detail = status.message;

    // Send name, nativeCode, and detail to the application's diagnostic system.
    static_cast<void>(name);
    static_cast<void>(nativeCode);
    static_cast<void>(detail);
}
```

Treat the portable `ErrorCode` as the primary decision field. Native codes and messages are supplemental diagnostics and are not stable machine-readable interfaces.

Read and write results may contain partial progress when the status is a failure. Preserve or discard that progress according to the calling operation's policy.

Use a finite `maxBytes` for externally controlled input. The limit is a hard accepted-size limit, not a truncation request.

## Where to go next

- @ref io_public_api inventories the complete public surface and package boundary.
- @ref io_reader_writer_contract defines transfer, lifetime, capability, and backend-extension rules.
- @ref io_error_model explains status selection and partial failures.
- @ref io_runtime_performance explains allocation and limit behavior.
- @ref io_examples provides complete usage and backend examples.
- @ref io_troubleshooting maps common symptoms to contract violations.
