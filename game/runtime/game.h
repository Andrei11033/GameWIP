/// @file game.h
/// @brief Runtime facade entered after process startup and optional validation.
///
/// The runtime facade is the executable-owned boundary between startup wiring
/// and game/runtime composition. It is not an installed reusable library API.

#pragma once

namespace GameWIP::Game
{
    /// @brief Runs the executable-owned game runtime.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @return Process exit code.
    int run(int argc, char **argv);
} // namespace GameWIP::Game
