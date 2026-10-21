/// @file accessibility_test.h
/// @brief Focused accessibility suites registered in the Desktop module.
#pragma once
#include "test_support/reporting.h"
namespace GameWIP::Test
{
    void runAccessibilityTests(TestSupport::Runner &runner);
    /// @brief Runs the isolated real UIA client/owner-pump protocol; its parent enforces a timeout.
    int runAccessibilityClientChild();
} // namespace GameWIP::Test
