@page io_examples Examples

All fallible operations return the shared `GameWIP::Status::Types::Result<T>` contract. See @ref status for result and diagnostic details.

## Read a bounded text stream

```cpp
std::string source = "settings=true";
GameWIP::IO::MemoryReader reader(source);
auto result = GameWIP::IO::readAllText(reader, 4096);

if(result.ok())
{
    configure(result.value());
}
else if(result.hasValue())
{
    // Keep only the complete valid UTF-8 prefix when useful.
    inspectPrefix(result.value());
}
```

## Inspect read progress

```cpp
std::array<std::byte, 64> buffer{};
auto result = reader.read(buffer);

if(result.ok())
{
    const auto progress = result.value();
    process(buffer.first(progress.bytesRead));
    if(progress.endOfStream)
    {
        finishInput();
    }
}
```

The final non-empty read may set `endOfStream`. A failing read may also have a partial `ReadProgress` value.

## Preserve write progress

```cpp
auto result = GameWIP::IO::writeAllBytes(writer, payload);
if(!result.ok() && result.hasValue())
{
    recordAcceptedPrefix(result.value().bytesWritten);
}
```

The helper retries successful short writes. A failure result retains total accepted progress.

## Implement a Writer

```cpp
class LimitedWriter final : public GameWIP::IO::Writer
{
public:
    GameWIP::Status::Types::Result<GameWIP::IO::Types::WriteProgress>
    write(std::span<const std::byte> bytes) noexcept override
    {
        const std::size_t accepted = std::min(bytes.size(), remainingCapacity_);
        remainingCapacity_ -= accepted;

        if(accepted != bytes.size())
        {
            return GameWIP::Status::Types::Result<GameWIP::IO::Types::WriteProgress>::failure(
                GameWIP::Status::Types::Status(GameWIP::Status::Types::Code::StorageFull),
                {.bytesWritten = accepted});
        }

        return GameWIP::Status::Types::Result<GameWIP::IO::Types::WriteProgress>::success({.bytesWritten = accepted});
    }

private:
    std::size_t remainingCapacity_ = 1024;
};
```

An override must not retain the input span and must contain expected failures. Throwing from a `noexcept` operation terminates the process.

## Inspect optional diagnostics

```cpp
const auto &status = result.status();
const auto code = status.code();

if(const auto *diagnostic = status.diagnostic())
{
    // Include the native code and message in developer-facing reports.
    recordDiagnostic(code, diagnostic->nativeCode, diagnostic->message);
}
```

Use `code` for decisions. Diagnostic text is UTF-8 and is not stable for parsing.
