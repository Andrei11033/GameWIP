#pragma once

#include <cstddef>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the assert test suite.
    struct AssertTestOptions
    {
        /// @brief Enables the child-process assertion failure test when assertions are compiled in.
        bool enableAssertFailureChildTest = true;
        /// @brief Enables lightweight passing-path performance metrics at the end of the suite.
        bool enablePerformanceMetrics = true;
        /// @brief Number of iterations used for each passing-path performance scenario.
        std::size_t performanceIterations = 100'000;
    };

    /// @brief Runs the assert library tests.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param options Test toggles for expensive or process-aborting scenarios.
    /// @return Zero when every assert test passes, nonzero otherwise.
    int runAssertTests(int argc, char **argv, const AssertTestOptions &options = {});
}
