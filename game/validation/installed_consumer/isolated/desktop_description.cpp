/// @file desktop_description.cpp
/// @brief Verifies the installed focused Window description header in isolation.

#include "desktop/description.h"

static_assert(std::holds_alternative<GameWIP::Desktop::Types::WindowedModeRequest>(GameWIP::Desktop::Types::WindowModes::windowed()));
