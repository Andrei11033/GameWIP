/// @file registry.h
/// @brief Registration contract for source-tree correctness-test modules.
///
/// Modules register statically so the standalone runner, startup validation, and
/// CTest module entries can share the same suite implementations.

#pragma once

#include "validation/tests/runner.h"

#include <span>
#include <string_view>

/// @brief Source-tree correctness-runner and module-registration interfaces.
namespace GameWIP::Validation::Tests
{
    /// @brief Arguments and shared policy passed to one registered module.
    struct ModuleInvocation
    {
        // Original process arguments
        int argc = 0;          ///< Borrowed original process argument count.
        char **argv = nullptr; ///< Borrowed original process argument values, valid for the callback duration.

        // Shared runner policy
        const RunOptions &options; ///< Shared runner policy reference, valid for the callback duration.

        // Report aggregation
        bool appendReport = false; ///< True when this module must append to an existing aggregate report.
    };

    /// @brief Function signature used to invoke one correctness-test module.
    /// @return Zero for pass and nonzero for failure; escaped exceptions are converted by the runner.
    using ModuleRunFunction = int (*)(const ModuleInvocation &invocation);
    /// @brief Function signature used to identify module-owned child-process arguments without executing the child operation.
    /// @return True only when the original process arguments belong to this module's child protocol.
    using ChildArgumentMatcher = bool (*)(int argc, char **argv);

    /// @brief Static registration record for one correctness-test module.
    struct Module
    {
        // Registration identity
        std::string_view name; ///< Stable command-line and CTest module name backed by process-lifetime storage.
        int order = 0;         ///< Stable startup order; name breaks equal-order ties.

        // Module callbacks
        ModuleRunFunction run = nullptr;                      ///< Module entry point.
        ChildArgumentMatcher handlesChildArguments = nullptr; ///< Optional matcher for child-process protocol arguments.
    };

    /// @brief Adds one module to the process-local registry during static initialization.
    ///
    /// The registration object is normally declared in a module.cpp translation
    /// unit owned by the validation module.
    class Registration
    {
    public:
        /// @brief Appends one module record to the process-local registry.
        /// @param module Registration whose string and callback storage must remain valid for the process lifetime.
        /// @note Intended for construction during static initialization before runner use; registration may allocate and is not thread-safe.
        explicit Registration(Module module);
    };

    /// @brief Returns a non-owning view of every process-local module registration.
    /// @return Registry storage in registration order.
    /// @warning The view is invalidated by any later registration; normal use calls this only after static initialization completes.
    [[nodiscard]] std::span<const Module> registeredModules() noexcept;
} // namespace GameWIP::Validation::Tests
