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
#include <utility>
#include <vector>

namespace GameWIP::Validation::Tests
{
    namespace
    {
        /// @brief Command-line module selection and exclusions resolved before execution.
        struct Selection
        {
            std::optional<std::string> module;
            std::set<std::string, std::less<>> excludedModules;
            bool conflictingModules = false;
        };

        /// @brief Records one requested module and detects incompatible selectors.
        void requestModule(Selection &selection, std::string module)
        {
            if (selection.module && *selection.module != module)
            {
                selection.conflictingModules = true;
                return;
            }
            selection.module = std::move(module);
        }

        /// @brief Returns whether the process arguments contain one exact option.
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

        /// @brief Returns the value following a matched argument prefix.
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

        /// @brief Applies supported command-line overrides and returns the requested module selection.
        Selection applyArguments(int argc, char **argv, RunOptions &options)
        {
            Selection selection;
            for (int index = 1; index < argc; ++index)
            {
                if (argv[index] == nullptr)
                {
                    continue;
                }

                const std::string_view argument(argv[index]);
                constexpr std::string_view modulePrefix = "--test-module=";
                if (argument.starts_with(modulePrefix))
                {
                    requestModule(selection, std::string(argument.substr(modulePrefix.size())));
                }
            }

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
                requestModule(selection, "test_support");
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
                    requestModule(selection, std::string(module));
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

        /// @brief Resolves relative reports beneath the GameWIP temp root and rejects parent traversal.
        /// @details Invalid or empty paths disable file reporting without changing test execution.
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

        /// @brief Copies static registrations into deterministic order for child routing and execution.
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

        /// @brief Rejects incomplete and duplicate module registrations before any module runs.
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

        /// @brief Converts an unexpected module exception into a normal failing module result.
        [[nodiscard]] int invokeModule(const Module &module, const ModuleInvocation &invocation) noexcept
        {
            try
            {
                return module.run(invocation);
            }
            catch (const std::exception &exception)
            {
                std::cerr << "Validation module '" << module.name << "' threw an exception: " << exception.what() << '\n';
            }
            catch (...)
            {
                std::cerr << "Validation module '" << module.name << "' threw an unknown exception.\n";
            }
            return 1;
        }
    } // namespace

    TestResult run(int argc, char **argv, RunOptions options)
    {
        const std::vector<Module> modules = sortedModules();
        if (!validModules(modules))
        {
            return {.modulesFailed = 1, .exitCode = 1};
        }

        const Module *childOwner = nullptr;
        for (const Module &module : modules)
        {
            bool handlesChildArguments = false;
            if (module.handlesChildArguments != nullptr)
            {
                try
                {
                    handlesChildArguments = module.handlesChildArguments(argc, argv);
                }
                catch (const std::exception &exception)
                {
                    std::cerr << "Validation child matcher for module '" << module.name << "' threw an exception: " << exception.what() << '\n';
                    return {.modulesFailed = 1, .exitCode = 1};
                }
                catch (...)
                {
                    std::cerr << "Validation child matcher for module '" << module.name << "' threw an unknown exception.\n";
                    return {.modulesFailed = 1, .exitCode = 1};
                }
            }

            if (handlesChildArguments)
            {
                if (childOwner != nullptr)
                {
                    std::cerr << "Ambiguous validation child invocation matched modules '" << childOwner->name << "' and '" << module.name << "'.\n";
                    return {.modulesFailed = 1, .exitCode = 1, .handledChildInvocation = true};
                }
                childOwner = &module;
            }
        }

        if (childOwner != nullptr)
        {
            const int exitCode = invokeModule(*childOwner, {argc, argv, options, false});
            return {
                .modulesRun = 1,
                .modulesFailed = exitCode == 0 ? 0u : 1u,
                .exitCode = exitCode,
                .handledChildInvocation = true,
            };
        }

        const Selection selection = applyArguments(argc, argv, options);
        if (selection.conflictingModules)
        {
            std::cerr << "Conflicting validation module selectors were provided.\n";
            return {.modulesFailed = 1, .exitCode = 1};
        }
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
        if (selection.module && selection.excludedModules.contains(*selection.module))
        {
            std::cerr << "Selected validation module is also excluded: " << *selection.module << '\n';
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

            const int moduleExitCode = invokeModule(module, {argc, argv, options, appendReport});
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
