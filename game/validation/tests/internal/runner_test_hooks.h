/// @file runner_test_hooks.h
/// @brief Test seam for running validation policy against explicit modules.

#pragma once

#include "validation/tests/registry.h"

#include <span>

namespace GameWIP::Validation::Tests::Internal::TestHooks
{
    /// @brief Runs the ordinary validation policy against an explicit module set instead of the process registry.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process argument values.
    /// @param options Runner policy copied and overridden by recognized arguments.
    /// @param registrations Borrowed module records that must remain valid for the call.
    /// @return The same aggregate or child-route result contract as Tests::run().
    /// @note Source-tree only; not installed or exposed as application API.
    [[nodiscard]] TestResult runWithModules(int argc, char **argv, RunOptions options, std::span<const Module> registrations);
} // namespace GameWIP::Validation::Tests::Internal::TestHooks
