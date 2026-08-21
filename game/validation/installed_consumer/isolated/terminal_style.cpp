/// @file terminal_style.cpp
/// @brief Verifies the installed focused Terminal style header in isolation.

#include "terminal/style.h"

static_assert(noexcept(GameWIP::Terminal::defaultColor()));
