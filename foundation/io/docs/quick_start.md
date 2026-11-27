@page io_quick_start Quick start

This example reads caller-owned bytes as UTF-8 text, then writes them to a memory-backed destination.

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

```cpp
#include "io/io.h"

#include <cstdint>
#include <string>

int main()
{
    std::string source = "jump=Space";
    GameWIP::IO::MemoryReader reader(source);

    constexpr std::uint64_t maximumConfigBytes = 16 * 1024;
    auto readResult = GameWIP::IO::readAllText(reader, maximumConfigBytes);
    if(!readResult.ok())
    {
        // Check hasValue() before using an available partial UTF-8 prefix.
        return 1;
    }

    GameWIP::IO::MemoryWriter writer;
    auto writeResult = GameWIP::IO::writeAllText(writer, readResult.value());
    if(!writeResult.ok())
    {
        return 1;
    }

    auto copy = writer.copyText();
    return copy.ok() && copy.value() == source ? 0 : 1;
}
```

`MemoryReader` borrows its source. Keep it alive at a stable address until the reader is no longer used. Temporary strings and byte vectors are
rejected, but caller-created dangling views cannot be detected.

## Failure handling

Use the portable code for decisions. Inspect optional diagnostics only when preparing a developer-facing report:

```cpp
void reportFailure(const GameWIP::Status::Types::Status &status)
{
    const auto code = status.code();
    if(const auto *detail = status.diagnostic())
    {
        reportStatus(code, detail->nativeCode, detail->message);
    }
    else
    {
        reportStatus(code, {}, {});
    }
}
```

Read the complete result contract in @ref status. IO uses strict UTF-8; see @ref io_utf8_policy. Set a finite
`maxBytes` for externally controlled input.

## Where to go next

- @ref io_public_api inventories the public surface and package boundary.
- @ref io_reader_writer_contract defines transfer, lifetime, and capability rules.
- @ref io_error_model covers IO-specific codes and partial failures.
- @ref io_runtime_performance explains allocation and limit behavior.
- @ref io_examples provides additional examples.
- @ref io_troubleshooting maps common symptoms to contract violations.
