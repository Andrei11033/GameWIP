/// @file game.cpp
/// @brief Game runtime entry point implementation.

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
