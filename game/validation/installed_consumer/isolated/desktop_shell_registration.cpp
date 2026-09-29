/// @file desktop_shell_registration.cpp
/// @brief Verifies the installed desktop/shell_registration.h header in isolation.

#include "desktop/shell_registration.h"

static_assert(noexcept(GameWIP::Desktop::Registration::registerFileExtension({})));
