/// @file accessibility_test.h
/// @brief Focused accessibility suites registered in the Desktop module.

#pragma once

#include "test_support/reporting.h"

namespace GameWIP::Test
{
    /// @brief Registers Desktop accessibility suites with the shared validation runner.
    void runAccessibilityTests(TestSupport::Runner &runner);

    /// @brief Runs the isolated real UIA client/owner-pump protocol; its parent enforces a timeout.
    [[nodiscard]] int runAccessibilityClientChild();
} // namespace GameWIP::Test
