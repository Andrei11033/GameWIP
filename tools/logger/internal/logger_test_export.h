/// @file logger_test_export.h
/// @brief Source-tree-only visibility annotation for Logger validation hooks.

#pragma once

#include "logger/logger_export.h"

// Source-tree validation ABI only. This header is not installed; LOGGER_EXPORT
// remains the installed production/runtime DLL ABI.
#define LOGGER_TEST_EXPORT LOGGER_EXPORT
