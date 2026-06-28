/// @file game.h
/// @brief Game runtime entry point used after optional startup validation.

#pragma once

namespace GameWIP::Game
{
    /// @brief Runs the game runtime.
    /// @return Process exit code.
    int run(int argc, char **argv);
} // namespace GameWIP::Game
