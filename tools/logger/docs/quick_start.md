@page logger_quick_start Quick start

This path starts one process-wide logger, submits a message, flushes it, and
shuts down cleanly. It demonstrates the lifecycle every larger configuration
builds upon.

## Include

```cpp
#include "logger/logger.h"
```

Use `logger/types.h` or `logger/config.h` for focused passive-type or
configuration access. For the operational API, `logger/record.h` contains
record metadata, `logger/message_format.h` contains message-color helpers,
`logger/logging.h` contains lifecycle, filtering, and asynchronous logging, and
`logger/reports.h` contains synchronous diagnostics. Include
`logger/logger_macros.h` only when using opt-in logging macros.

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock;
see @ref project_library_compatibility.

```cmake
find_package(Logger ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Logger)
```

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE Logger)
```

## Minimal usage

```cpp
#include "logger/logger.h"

#include <chrono>

int main()
{
    using namespace std::chrono_literals;

    const auto init =
        GameWIP::Logger::initConsole(GameWIP::Logger::Types::Level::Debug);
    if(!init.ok())
    {
        return 1;
    }

    GameWIP::Logger::info("Startup", "ready {}", 1);

    const auto flush = GameWIP::Logger::flush(500ms);
    if(!flush.ok() || flush.value().outcome != GameWIP::Logger::Types::FlushOutcome::Completed)
    {
        static_cast<void>(GameWIP::Logger::shutdown());
        return 2;
    }

    const auto shutdown = GameWIP::Logger::shutdown();
    return shutdown.ok() ? 0 : 3;
}
```

Normal log calls are filterable and asynchronous. `flush()` drains accepted
queued records and flushes active sinks; `shutdown()` performs best-effort
draining and always leaves Logger disabled.

## Failure handling

Initialization returns `Types::Init::Result`. Check `ok()` before reading
`value()`. The payload reports the outcome, requested/effective output,
adjustments, and any recovered setup failure. `status()` gives the exact
operation status. Asynchronous sink failures are observed through `getHealth()`.

Use `reportError()` or `reportFatal()` for synchronous emergency diagnostics.
Reports do not drain older queued logs; call `flush()` first when backlog
completion is required.

## Where to go next

- @ref logger_public_api inventories the public surface.
- @ref logger_configuration explains presets, limits, and compile-time controls.
- @ref logger_lifecycle defines initialization, flush, shutdown, and reinitialization.
- @ref logger_reports defines synchronous delivery and timeout behavior.
- @ref logger_examples provides complete configuration and reporting examples.
- @ref logger_troubleshooting maps common failures to their owning contract.
