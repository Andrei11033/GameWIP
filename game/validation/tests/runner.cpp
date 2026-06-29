/// @file runner.cpp
/// @brief Selection, child routing, and aggregation for correctness-test modules.

#include "validation/tests/runner.h"

#include "validation/tests/registry.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Validation::Tests
{
    namespace
    {
        struct Selection
        {
            std::optional<std::string> module;
            std::set<std::string, std::less<>> excludedModules;
        };

        [[nodiscard]] bool hasArgument(int argc, char **argv, std::string_view argument)
        {
            for (int index = 1; index < argc; ++index)
            {
                if (argv[index] != nullptr && std::string_view(argv[index]) == argument)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::optional<std::string> argumentValue(int argc, char **argv, std::string_view prefix)
        {
            for (int index = 1; index < argc; ++index)
            {
                if (argv[index] == nullptr)
                {
                    continue;
                }

                const std::string_view argument(argv[index]);
                if (argument.starts_with(prefix))
                {
                    return std::string(argument.substr(prefix.size()));
                }
            }
            return std::nullopt;
        }

        Selection applyArguments(int argc, char **argv, RunOptions &options)
        {
            Selection selection;
            selection.module = argumentValue(argc, argv, "--test-module=");

            if (const auto reportPath = argumentValue(argc, argv, "--test-report="))
            {
                options.reportPath = *reportPath;
            }
            if (hasArgument(argc, argv, "--no-test-report"))
            {
                options.writeReport = false;
            }
            if (hasArgument(argc, argv, "--verbose-tests"))
            {
                options.verboseConsole = true;
            }
            if (hasArgument(argc, argv, "--no-manual-ui"))
            {
                options.enableManualUiTests = false;
                options.enableLoggerPopupTest = false;
            }
            if (hasArgument(argc, argv, "--no-logger-popup"))
            {
                options.enableLoggerPopupTest = false;
            }
            if (hasArgument(argc, argv, "--no-test-support-child-process"))
            {
                options.enableTestSupportChildProcessTests = false;
            }
            if (hasArgument(argc, argv, "--test-support-manual"))
            {
                selection.module = "test_support";
                options.enableManualUiTests = true;
            }

            const std::pair<std::string_view, std::string_view> focusedAliases[] = {
                {"--io-only", "io"},
                {"--filesystem-only", "filesystem"},
                {"--terminal-only", "terminal"},
                {"--test-support-only", "test_support"},
            };
            for (const auto &[argument, module] : focusedAliases)
            {
                if (hasArgument(argc, argv, argument))
                {
                    selection.module = std::string(module);
                }
            }

            const std::pair<std::string_view, std::string_view> exclusionAliases[] = {
                {"--no-io-tests", "io"},
                {"--no-filesystem-tests", "filesystem"},
                {"--no-terminal-tests", "terminal"},
            };
            for (const auto &[argument, module] : exclusionAliases)
            {
                if (hasArgument(argc, argv, argument))
                {
                    selection.excludedModules.emplace(module);
                }
            }

            return selection;
        }

        void resolveReportOutput(RunOptions &options)
        {
            if (!options.writeReport)
            {
                return;
            }
            if (options.reportPath.empty())
            {
                std::cerr << "[VALIDATION] report disabled: the requested path is empty.\n";
                options.writeReport = false;
                return;
            }

            try
            {
                if (!options.reportPath.is_absolute())
                {
                    const std::filesystem::path normalized = options.reportPath.lexically_normal();
                    for (const std::filesystem::path &component : normalized)
                    {
                        if (component == "..")
                        {
                            throw std::invalid_argument("relative report paths cannot contain '..'");
                        }
                    }
                    options.reportPath = (std::filesystem::temp_directory_path() / "GameWIP" / normalized).lexically_normal();
                }
                else
                {
                    options.reportPath = options.reportPath.lexically_normal();
                }
            }
            catch (const std::exception &exception)
            {
                std::cerr << "[VALIDATION] report disabled: " << exception.what() << '\n';
                options.writeReport = false;
            }
        }

        [[nodiscard]] std::vector<Module> sortedModules()
        {
            const std::span<const Module> registrations = registeredModules();
            std::vector<Module> modules(registrations.begin(), registrations.end());
            std::ranges::sort(
                modules,
                {},
                [](const Module &module)
                {
                    return std::pair(module.order, module.name);
                });
            return modules;
        }

        [[nodiscard]] bool validModules(const std::vector<Module> &modules)
        {
            std::set<std::string_view> names;
            for (const Module &module : modules)
            {
                if (module.name.empty() || module.run == nullptr)
                {
                    std::cerr << "Invalid validation module registration.\n";
                    return false;
                }
                if (!names.insert(module.name).second)
                {
                    std::cerr << "Duplicate validation module: " << module.name << '\n';
                    return false;
                }
            }
            return true;
        }
    } // namespace

    TestResult run(int argc, char **argv, RunOptions options)
    {
        const std::vector<Module> modules = sortedModules();
        if (!validModules(modules))
        {
            return {.modulesFailed = 1, .exitCode = 1};
        }

        for (const Module &module : modules)
        {
            if (module.handlesChildArguments != nullptr && module.handlesChildArguments(argc, argv))
            {
                const int exitCode = module.run({argc, argv, options, false});
                return {
                    .modulesRun = 1,
                    .modulesFailed = exitCode == 0 ? 0u : 1u,
                    .exitCode = exitCode,
                    .handledChildInvocation = true,
                };
            }
        }

        const Selection selection = applyArguments(argc, argv, options);
        if (selection.module && std::ranges::none_of(
                                    modules,
                                    [&](const Module &module)
                                    {
                                        return module.name == *selection.module;
                                    }))
        {
            std::cerr << "Unknown validation module: " << *selection.module << '\n';
            return {.modulesFailed = 1, .exitCode = 1};
        }

        resolveReportOutput(options);
        if (options.writeReport)
        {
            std::cout << "[VALIDATION] report=" << options.reportPath.string() << '\n';
        }

        TestResult result;
        bool appendReport = options.appendReport;
        for (const Module &module : modules)
        {
            if ((selection.module && module.name != *selection.module) || selection.excludedModules.contains(module.name))
            {
                continue;
            }

            const int moduleExitCode = module.run({argc, argv, options, appendReport});
            ++result.modulesRun;
            if (moduleExitCode != 0)
            {
                ++result.modulesFailed;
                result.exitCode = 1;
            }
            std::cout << "[VALIDATION] module=" << module.name << " result=" << (moduleExitCode == 0 ? "PASS" : "FAIL")
                      << " exitCode=" << moduleExitCode << '\n';
            appendReport = appendReport || options.writeReport;
        }

        std::cout << "[VALIDATION] result=" << (result.ok() ? "PASS" : "FAIL") << " modules=" << result.modulesRun
                  << " failed=" << result.modulesFailed << '\n';
        return result;
    }
} // namespace GameWIP::Validation::Tests
