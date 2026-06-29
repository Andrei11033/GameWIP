/// @file registry.cpp
/// @brief Storage for modular correctness-test registrations.

#include "validation/tests/registry.h"

#include <vector>

namespace GameWIP::Validation::Tests
{
    namespace
    {
        /// @brief Owns registrations without depending on cross-translation-unit initialization order.
        std::vector<Module> &moduleStorage()
        {
            static std::vector<Module> modules;
            return modules;
        }
    } // namespace

    Registration::Registration(Module module)
    {
        moduleStorage().push_back(module);
    }

    std::span<const Module> registeredModules() noexcept
    {
        return moduleStorage();
    }
} // namespace GameWIP::Validation::Tests
