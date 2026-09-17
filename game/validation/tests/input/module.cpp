/// @file module.cpp
/// @brief Registers the Input correctness-test module.

#include "validation/tests/input/input_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &)
    {
        return GameWIP::Test::runInputTests();
    }

    const GameWIP::Validation::Tests::Registration kRegistration({.name = "input", .order = 20, .run = run});
} // namespace
