/// @file desktop_description.cpp
/// @brief Verifies the installed focused Window description header in isolation.

#include "desktop/description.h"

static_assert(GameWIP::Desktop::Types::ModeRequest{}.mode == GameWIP::Desktop::Types::Mode::Windowed);
