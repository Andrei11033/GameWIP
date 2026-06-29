#include "validation/tests/filesystem/filesystem_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::FileSystemTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runFileSystemTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "filesystem",
        .order = 20,
        .run = run,
    });
} // namespace
