@page logger_quick_start Quick start

```cpp
#include "logger/logger.h"

using namespace std::chrono_literals;

const auto init = GameWIP::Logger::initConsole(GameWIP::Logger::Types::Level::Debug);
if (!init.status.ok())
    return;

GameWIP::Logger::info("Startup", "ready {}", 1);

const auto flush = GameWIP::Logger::flush(500ms);
static_cast<void>(flush);
static_cast<void>(GameWIP::Logger::shutdown());
```

For file startup, inspect both overall status and `outputSetupStatus` when fallback matters. For emergency diagnostics use `reportError()` or `reportFatal()`; these write synchronously and do not wait for older queued logs.

Installed consumers link `GameWIP::Logger`. Logger's public API exposes `IO::Types::Status`, so the installed Logger package discovers IO transitively.
