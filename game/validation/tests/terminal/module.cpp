#include "validation/tests/terminal/terminal_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::TerminalTestOptions options;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runTerminalTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "terminal",
        .order = 30,
        .run = run,
    });
} // namespace
