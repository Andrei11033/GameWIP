#include "validation/tests/io/io_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::IOTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runIOTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "io",
        .order = 10,
        .run = run,
    });
} // namespace
