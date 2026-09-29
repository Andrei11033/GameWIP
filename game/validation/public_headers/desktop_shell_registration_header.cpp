/// @file desktop_shell_registration_header.cpp
/// @brief Verifies desktop/shell_registration.h in isolation.

#include "desktop/shell_registration.h"

static_assert(noexcept(GameWIP::Desktop::Registration::registerFileExtension({})));
