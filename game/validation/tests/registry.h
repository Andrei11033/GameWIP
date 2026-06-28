/// @file registry.h
/// @brief Registration contract for modular correctness-test suites.

#pragma once

#include "validation/tests/runner.h"

#include <span>
#include <string_view>

namespace GameWIP::Validation::Tests
{
    struct ModuleInvocation
    {
        int argc = 0;
        char **argv = nullptr;
        const RunOptions &options;
        bool appendReport = false;
    };

    using ModuleRunFunction = int (*)(const ModuleInvocation &invocation);
    using ChildArgumentMatcher = bool (*)(int argc, char **argv);

    struct Module
    {
        std::string_view name;
        int order = 0;
        ModuleRunFunction run = nullptr;
        ChildArgumentMatcher handlesChildArguments = nullptr;
    };

    class Registration
    {
    public:
        explicit Registration(Module module);
    };

    [[nodiscard]] std::span<const Module> registeredModules() noexcept;
} // namespace GameWIP::Validation::Tests
