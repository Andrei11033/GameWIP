/// @file registry.cpp
/// @brief Storage for modular correctness-test registrations.

#include "validation/tests/registry.h"

#include <vector>

namespace GameWIP::Validation::Tests
{
    namespace
    {
        /// @brief Lazily owns module records without depending on another translation unit's initialization order.
        /// @note Registration still occurs during static initialization and must finish before the returned registry view is used.
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
