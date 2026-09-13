/// @file assert_test_export.h
/// @brief Source-tree-only visibility annotation for Assert validation hooks.

#pragma once

#include "debug/assert/assert_export.h"

// Source-tree validation ABI only. This header is not installed; ASSERT_EXPORT
// remains the installed production/runtime DLL ABI.
#define ASSERT_TEST_EXPORT ASSERT_EXPORT
