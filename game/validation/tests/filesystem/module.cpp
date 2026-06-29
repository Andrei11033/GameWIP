/// @file module.cpp
/// @brief Registers the FileSystem correctness-test module with the validation runner.

#include "validation/tests/filesystem/filesystem_test.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner policy to FileSystem-specific test options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::FileSystemTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runFileSystemTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic FileSystem module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "filesystem",
        .order = 20,
        .run = run,
    });
} // namespace
