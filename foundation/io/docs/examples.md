@page io_examples Examples

The examples use only installed public APIs.

## Read bounded text from stable memory

```cpp
#include "io/io.h"

#include <string>
#include <utility>

bool loadSmallConfig(std::string& output)
{
    std::string source = "width=1920\nheight=1080\n";
    GameWIP::IO::MemoryReader reader(source);

    GameWIP::IO::Types::ReadAllTextResult result =
        GameWIP::IO::readAllText(reader, 64 * 1024);

    if (!result.status.ok())
    {
        return false;
    }

    output = std::move(result.text);
    return true;
}
```

The source string outlives the reader. `readAllText()` requires strict UTF-8 and returns only a complete valid UTF-8 prefix on failure. The limit
rejects oversized input rather than truncating it.

## Seek and read one byte

```cpp
#include "io/io.h"

#include <array>
#include <cstddef>
#include <vector>

bool readThirdByte(std::byte& output)
{
    const std::vector<std::byte> source{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};

    GameWIP::IO::MemoryReader reader(source);
    const GameWIP::IO::Types::Status seekStatus =
        reader.seek(2, GameWIP::IO::Types::SeekOrigin::Begin);

    if (!seekStatus.ok())
    {
        return false;
    }

    std::array<std::byte, 1> destination{};
    const GameWIP::IO::Types::ReadResult readResult = reader.read(destination);

    if (!readResult.status.ok() || readResult.bytesRead != 1)
    {
        return false;
    }

    output = destination[0];
    return true;
}
```

## Reuse scratch storage for unknown-size readers

```cpp
#include "io/io.h"

#include <array>
#include <cstddef>
#include <span>

GameWIP::IO::Types::ReadAllBytesResult readBoundedStream(
    GameWIP::IO::Reader& reader)
{
    std::array<std::byte, 4096> scratch{};
    return GameWIP::IO::readAllBytes(
        reader,
        std::span<std::byte>(scratch),
        8 * 1024 * 1024);
}
```

The scratch storage is used only when size or position is unavailable. It may be overwritten and is not retained after the call.

## Reuse writer capacity

```cpp
#include "io/io.h"

#include <string>
#include <string_view>

bool encodeTwice(GameWIP::IO::MemoryWriter& writer)
{
    if (!writer.reserve(4096).ok())
    {
        return false;
    }

    writer.clear();
    if (!GameWIP::IO::writeAllText(writer, "first").status.ok())
    {
        return false;
    }

    GameWIP::IO::Types::CopyTextResult firstCopy = writer.copyText();
    if (!firstCopy.status.ok())
    {
        return false;
    }

    writer.clear();
    if (!GameWIP::IO::writeAllText(writer, "second").status.ok())
    {
        return false;
    }

    GameWIP::IO::Types::CopyTextResult secondCopy = writer.copyText();
    return secondCopy.status.ok() && firstCopy.text == "first" && secondCopy.text == "second";
}
```

`clear()` preserves capacity. `writeAllText()` validates before writing and `copyText()` validates the collected bytes before copying. A view returned
by `bytes()` must not be retained across either clear or a later write.

## Transfer ownership from MemoryWriter

```cpp
#include "io/io.h"

#include <cstddef>
#include <vector>

std::vector<std::byte> buildPacket()
{
    GameWIP::IO::MemoryWriter writer;
    if (!writer.reserve(128).ok())
    {
        return {};
    }
    const std::vector<std::byte> header{
        std::byte{0x47}, std::byte{0x57}, std::byte{0x49}, std::byte{0x50}};

    const GameWIP::IO::Types::WriteResult result =
        GameWIP::IO::writeAllBytes(writer, header);

    if (!result.status.ok())
    {
        return {};
    }

    return writer.takeBytes();
}
```

`takeBytes()` leaves the writer empty and preserves whether it was open or closed.

## Inspect a failure

```cpp
#include "io/io.h"

#include <string_view>

std::string_view portableFailureName(const GameWIP::IO::Types::Status& status)
{
    return GameWIP::IO::errorCodeName(status.code);
}
```

Use the portable code for control flow. Treat `nativeCode` and `message` as supplemental diagnostics.

## Create a backend status

```cpp
#include "io/io.h"

#include <cstdint>

GameWIP::IO::Types::Status permissionFailure(std::int64_t platformCode)
{
    return GameWIP::IO::makeStatus(
        GameWIP::IO::Types::ErrorCode::PermissionDenied,
        platformCode,
        "opening the resource was denied");
}
```

Prefer the specific portable category over a generic operation failure.

## Implement a minimal writer

A stateless or externally owned adapter can implement only `write()` and inherit the neutral lifecycle and capability defaults:

```cpp
#include "io/io.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>

class FixedBufferWriter final : public GameWIP::IO::Writer
{
public:
    explicit FixedBufferWriter(std::span<std::byte> destination) noexcept
        : destination_(destination)
    {
    }

    [[nodiscard]] GameWIP::IO::Types::WriteResult write(
        std::span<const std::byte> bytes) noexcept override
    {
        const std::size_t available = destination_.size() - position_;
        const std::size_t accepted = std::min(available, bytes.size());

        if (accepted != 0)
        {
            std::memcpy(destination_.data() + position_, bytes.data(), accepted);
            position_ += accepted;
        }

        if (accepted != bytes.size())
        {
            return {
                .status = GameWIP::IO::makeStatus(
                    GameWIP::IO::Types::ErrorCode::StorageFull),
                .bytesWritten = accepted};
        }

        return {
            .status = GameWIP::IO::successStatus(),
            .bytesWritten = accepted};
    }

private:
    std::span<std::byte> destination_;
    std::size_t position_ = 0;
};
```

The adapter does not retain any input span. It reports accepted progress with the capacity failure, allowing `writeAllBytes()` to return the exact
total.

## Related pages

- @ref io_quick_start
- @ref io_reader_writer_contract
- @ref io_error_model
