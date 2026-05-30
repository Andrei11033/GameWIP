@page logger_examples Logger examples

## Normal preformatted logging

```cpp
GameWIP::Logger::info("Game", "Game started");
GameWIP::Logger::warn("Assets", "Missing optional asset");
```

## Lazy formatted logging through macros

```cpp
LOGGER_DEBUG(LogSource::Renderer, "Visible meshes: {}", visibleMeshCount);
LOGGER_ERROR("SaveSystem", "Save failed: {}", errorText);
```

The `LOGGER_*` macros call `shouldLog(...)` before evaluating message/format arguments, so filtered messages avoid unnecessary work.

## Runtime format strings

```cpp
const std::string_view formatFromConfig = "Player {} joined";
GameWIP::Logger::info(
    "Network",
    GameWIP::Logger::runtimeFormat(formatFromConfig),
    playerName);
```

Use `runtimeFormat(...)` only when the format string cannot be known at compile time.

## Synchronous report

```cpp
GameWIP::Logger::report(
    GameWIP::Logger::Types::Level::Error,
    "SaveSystem",
    "Could not open save file");
```

## Fatal popup report

```cpp
GameWIP::Logger::report(
    GameWIP::Logger::Types::Level::Fatal,
    "Startup",
    GameWIP::Logger::Types::ReportPopup::Fatal,
    "Critical startup failure");
```

## Terminating fatal path

```cpp
GameWIP::Logger::fatalTerminate("Startup", "Required asset database is missing");
```

`fatalTerminate(...)` does not return. For a non-terminating fatal-severity queue entry, use `Logger::fatal(...)` instead.
