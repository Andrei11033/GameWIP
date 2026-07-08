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
    /// @brief Runs validation against an explicit module set instead of the process registry.
    /// @note This is a source-tree testing interface and is not installed as public API.
    [[nodiscard]] TestResult runWithModules(int argc, char **argv, RunOptions options, std::span<const Module> registrations);
} // namespace GameWIP::Validation::Tests::Detail
