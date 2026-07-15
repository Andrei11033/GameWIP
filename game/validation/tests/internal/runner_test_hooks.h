/// @file runner_test_hooks.h
/// @brief Source-tree hooks for testing validation-runner policy with explicit module sets.
///
/// These hooks let runner self-tests bypass the process-wide static registry so
/// they can exercise ordering, selection, and conflict handling deterministically.

#pragma once

#include "validation/tests/registry.h"

#include <span>

namespace GameWIP::Validation::Tests::Detail
{
    /// @brief Runs the ordinary validation policy against an explicit module set instead of the process registry.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process argument values.
    /// @param options Runner policy copied and overridden by recognized arguments.
    /// @param registrations Borrowed module records that must remain valid for the call.
    /// @return The same aggregate or child-route result contract as Tests::run().
    /// @note This approved source-tree test seam is not installed and is not application or module API.
    [[nodiscard]] TestResult runWithModules(int argc, char **argv, RunOptions options, std::span<const Module> registrations);
} // namespace GameWIP::Validation::Tests::Detail
