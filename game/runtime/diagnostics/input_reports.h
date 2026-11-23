/// @file input_reports.h
/// @brief Shared logging entry point for retained raw Input reports.

#pragma once

#include "input/frame.h"

namespace GameWIP::Game::RuntimeDiagnostics
{
    /// @brief Logs bounded report previews and selected-platform details at Trace.
    void logRawInputReports(const GameWIP::Input::Types::Frame &frame);
} // namespace GameWIP::Game::RuntimeDiagnostics
