/// @file game.cpp
/// @brief Implementation of the executable-owned runtime facade.
///
/// This file owns executable runtime composition. Keep process startup policy in
/// main.cpp, return expected runtime failures as process exit codes, and place
/// reusable behavior in the owning reusable library.

#include "runtime/game.h"

#include "logger/logger.h"

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <cstdlib>

namespace GameWIP::Game
{
    int run(int argc, char **argv)
    {
#if GAMEWIP_TRACY_ENABLED
        FrameMark;
        ZoneScopedN("Game runtime");
#endif
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedN("Init Logger");
#endif
            GameWIP::Logger::initConsole(GameWIP::Logger::Types::Level::Debug);
            GameWIP::Logger::info("Startup", "Logger initialized");
        }

        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedN("Logger shutdown");
#endif
            GameWIP::Logger::warn("Shutdown", "Logger shutting down");
            GameWIP::Logger::shutdown();
        }

        static_cast<void>(argc);
        static_cast<void>(argv);
        return EXIT_SUCCESS;
    }
} // namespace GameWIP::Game
