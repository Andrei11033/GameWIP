/// @file registry.h
/// @brief Registration contract for modular correctness-test suites.

#pragma once

#include "validation/tests/runner.h"

#include <span>
#include <string_view>

namespace GameWIP::Validation::Tests
{
    /// @brief Arguments and shared policy passed to one registered module.
    struct ModuleInvocation
    {
        /// @brief Process argument count.
        int argc = 0;
        /// @brief Process argument values.
        char **argv = nullptr;
        /// @brief Shared runtime options owned by the validation runner.
        const RunOptions &options;
        /// @brief True when this module must append to an existing aggregate report.
        bool appendReport = false;
    };

    /// @brief Function signature used to invoke one correctness-test module.
    using ModuleRunFunction = int (*)(const ModuleInvocation &invocation);
    /// @brief Function signature used to identify module-owned child-process arguments.
    using ChildArgumentMatcher = bool (*)(int argc, char **argv);

    /// @brief Static registration record for one correctness-test module.
    struct Module
    {
        /// @brief Stable command-line and CTest module name.
        std::string_view name;
        /// @brief Stable startup order; name breaks equal-order ties.
        int order = 0;
        /// @brief Module entry point.
        ModuleRunFunction run = nullptr;
        /// @brief Optional matcher for child-process protocol arguments.
        ChildArgumentMatcher handlesChildArguments = nullptr;
    };

    /// @brief Adds one module to the process-local registry during static initialization.
    class Registration
    {
    public:
        /// @brief Stores module in the process-local registry.
        explicit Registration(Module module);
    };

    /// @brief Returns every process-local module registration.
    [[nodiscard]] std::span<const Module> registeredModules() noexcept;
} // namespace GameWIP::Validation::Tests
