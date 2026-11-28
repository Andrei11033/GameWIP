/// @file runtime_input.h
/// @brief Runtime input session and selected platform attachment.

#pragma once

#if defined(_WIN32)
#include "runtime/platform/win32/runtime_input.h"
#else
#error "GameWIP runtime has no Input backend for this platform."
#endif
