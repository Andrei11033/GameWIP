/// @file input_reports.h
/// @brief Logs bounded native input report details at Trace.

#pragma once

#include "input/frame.h"

namespace GameWIP::Game::RuntimeDiagnostics
{
    void logRawInputReports(const GameWIP::Input::Types::Frame &frame);
} // namespace GameWIP::Game::RuntimeDiagnostics
