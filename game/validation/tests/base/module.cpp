/// @file module.cpp
/// @brief Registers the internal Base correctness-test module.

#include "validation/tests/base/base_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &)
    {
        return GameWIP::Test::runBaseTests();
    }

    const GameWIP::Validation::Tests::Registration registration({.name = "base", .order = 5, .run = run});
} // namespace

