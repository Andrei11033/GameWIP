/// @file input_backends.h
/// @brief Internal Win32 input backend diagnostics.

#pragma once

#include "input/win32.h"

namespace GameWIP::Game::RuntimeDiagnostics::Win32
{
    /// ------------------------------------------------------------
    /// Backend status reporting
    /// ------------------------------------------------------------

    /// @brief Logs selected and available Win32 input backend states at Trace.
    void logInputBackends(const GameWIP::Input::Platform::Win32::Attachment &attachment);
} // namespace GameWIP::Game::RuntimeDiagnostics::Win32
