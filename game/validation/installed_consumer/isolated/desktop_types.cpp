/// @file desktop_types.cpp
/// @brief Verifies the installed focused Window types header in isolation.

#include "desktop/types.h"

static_assert(GameWIP::Desktop::Types::Mode::Windowed != GameWIP::Desktop::Types::Mode::ExclusiveFullscreen);
