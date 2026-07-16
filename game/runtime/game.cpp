/// @file game.cpp
/// @brief Implementation of the executable-owned runtime facade.
///
/// This file is intentionally small until game/runtime composition moves behind
/// more specific runtime systems. Keep process startup policy in main.cpp, return
/// expected runtime failures as process exit codes, and place reusable behavior in
/// the owning reusable library.

#include "runtime/game.h"

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <cstdlib>

namespace GameWIP::Game
{
    int run(int argc, char **argv)
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedN("Game runtime");
#endif

        static_cast<void>(argc);
        static_cast<void>(argv);
        return EXIT_SUCCESS;
    }
} // namespace GameWIP::Game
