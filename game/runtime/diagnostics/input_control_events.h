/// @file input_control_events.h
/// @brief Internal entry points for opt-in Input control reporting.
#pragma once

#include "input/system.h"
#include "input/types.h"

#include <cstdint>

namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
{
    // ------------------------------------------------------------
    // Debug control changes
    // ------------------------------------------------------------

    /// @brief Logs a concise boolean control transition at Debug.
    void logChange(GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event);

    /// @brief Logs a raw numeric value, pointer position, or relative delta at Debug.
    void logRawChange(GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event);

    // ------------------------------------------------------------
    // Trace events
    // ------------------------------------------------------------

    /// @brief Logs a detailed Input event at Trace.
    void logTrace(GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event, std::uint64_t captureSequence);
} // namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
