/// @file desktop_result_header.cpp
/// @brief Verifies the Desktop Result public header in isolation.

#include "desktop/result.h"

#include <string>

static_assert(GameWIP::Desktop::Types::Result<int>{}.value == std::nullopt);
static_assert(!GameWIP::Desktop::Types::Result<int>{}.ok());
