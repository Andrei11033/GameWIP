/// @file assert_test_hooks.h
/// @brief Minimal source-tree-only Assert validation seam.

#pragma once

#include "debug/assert/assert_export.h"

#ifndef ASSERT_INTERNAL_TEST_HOOKS
#define ASSERT_INTERNAL_TEST_HOOKS 0
#endif

#if ASSERT_INTERNAL_TEST_HOOKS
namespace GameWIP::Debug::Assert::TestHooks
{
    /// @brief Validates diagnostic-preparation failure handling inside the noexcept popup boundary.
    /// @return True only when the armed one-shot failure was consumed by the real popup path.
    /// @warning Test-only and source-tree-only. Final native presentation is suppressed for the current test thread.
    [[nodiscard]] ASSERT_EXPORT bool runDiagnosticPreparationEmergencyPathForTest() noexcept;
} // namespace GameWIP::Debug::Assert::TestHooks
#endif
