/// @file game.h
/// @brief Runtime facade entered after process startup and optional validation.
///
/// The runtime facade is the executable-owned boundary between startup wiring
/// and game/runtime composition. It is not an installed reusable library API.

#pragma once

/// @brief Executable-owned runtime integration entered after process startup succeeds.
namespace GameWIP::Game
{
    /// @brief Runs the executable-owned game runtime after startup validation succeeds.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process argument values; the runtime must copy data it retains beyond the call.
    /// @return Process exit code returned by the GameWIP executable.
    /// @note Expected runtime startup or shutdown failures should be expressed through the return value;
    /// `main()` does not add a general exception boundary.
    int run(int argc, char **argv);
} // namespace GameWIP::Game
