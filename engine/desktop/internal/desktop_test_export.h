/// @file desktop_test_export.h
/// @brief Source-tree-only visibility annotation for Desktop validation hooks.

#pragma once

#include "desktop/desktop_export.h"

// Source-tree validation ABI only. This header is not installed; DESKTOP_EXPORT
// remains the installed production/runtime DLL ABI.
#define DESKTOP_TEST_EXPORT DESKTOP_EXPORT
