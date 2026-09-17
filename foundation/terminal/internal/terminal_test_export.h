/// @file terminal_test_export.h
/// @brief Source-tree-only visibility annotation for Terminal validation hooks.

#pragma once

#include "terminal/terminal_export.h"

// Source-tree validation ABI only. This header is not installed; TERMINAL_EXPORT
// remains the installed production/runtime DLL ABI.
#define TERMINAL_TEST_EXPORT TERMINAL_EXPORT
