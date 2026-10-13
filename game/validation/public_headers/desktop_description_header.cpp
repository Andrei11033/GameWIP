/// @file desktop_description_header.cpp
/// @brief Verifies the source-tree focused Window description header.

#include "desktop/description.h"

static_assert(GameWIP::Desktop::Types::Controls{}.closable);
static_assert(std::holds_alternative<GameWIP::Desktop::Types::WindowedModeRequest>(GameWIP::Desktop::Types::WindowModes::windowed()));
