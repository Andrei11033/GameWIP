/// @file runner.cpp
/// @brief Selection, child routing, and aggregation for correctness-test modules.

#include "validation/tests/runner.h"

#include "validation/process_arguments.h"
#include "validation/tests/internal/runner_test_hooks.h"
#include "validation/tests/registry.h"

#include "test_support/process.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
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
        [[nodiscard]] bool hasArgument(ProcessArguments arguments, std::string_view argument)
        {
            for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
            {
                if (value != nullptr && std::string_view(value) == argument)
                {
                    return true;
                }
            }
            return false;
        }

        /// @brief Returns the value following a matched argument prefix.
        [[nodiscard]] std::optional<std::string> argumentValue(ProcessArguments arguments, std::string_view prefix)
        {
            for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
            {
                if (value == nullptr)
                {
                    continue;
                }

                const std::string_view argument(value);
                if (argument.starts_with(prefix))
                {
                    return std::string(argument.substr(prefix.size()));
                }
            }
            return std::nullopt;
        }

        /// @brief Applies supported command-line overrides and returns the requested module selection.
        Selection applyArguments(ProcessArguments arguments, RunOptions &options)
        {
            Selection selection;
            for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
            {
                if (value == nullptr)
                {
                    continue;
                }

                const std::string_view argument(value);
                constexpr std::string_view modulePrefix = "--test-module=";
                if (argument.starts_with(modulePrefix))
                {
                    requestModule(selection, std::string(argument.substr(modulePrefix.size())));
                    continue;
                }

                constexpr std::string_view skipModulePrefix = "--skip-test-module=";
                if (argument.starts_with(skipModulePrefix))
                {
                    selection.excludedModules.emplace(argument.substr(skipModulePrefix.size()));
                }
            }

            if (const auto reportPath = argumentValue(arguments, "--test-report="))
            {
                options.reportPath = *reportPath;
            }
            if (hasArgument(arguments, "--no-test-report"))
            {
                options.writeReport = false;
            }
            if (hasArgument(arguments, "--verbose-tests"))
            {
                options.verboseConsole = true;
            }
            if (hasArgument(arguments, "--manual-tests"))
            {
                options.enableManualTests = true;
            }
            if (hasArgument(arguments, "--no-test-support-child-process"))
            {
                options.enableTestSupportChildProcessTests = false;
            }

            return selection;
        }

        /// @brief Returns the normalized directory that owns outputs for this executable invocation.
        [[nodiscard]] std::filesystem::path executableDirectory(ProcessArguments arguments)
        {
            if (!arguments.empty() && arguments.front() != nullptr && !std::string_view(arguments.front()).empty())
            {
                const std::filesystem::path executablePath = std::filesystem::absolute(arguments.front()).lexically_normal();
                if (executablePath.has_parent_path())
                {
                    return executablePath.parent_path();
                }
            }
            return std::filesystem::current_path();
        }

        /// @brief Resolves ordinary relative reports beneath the running executable directory and rejects parent traversal.
        /// @details Invalid or empty paths disable file reporting without changing test execution.
        void resolveReportOutput(const std::filesystem::path &outputRoot, RunOptions &options)
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
                    if (options.reportPath.has_root_name() || options.reportPath.has_root_directory())
                    {
                        throw std::invalid_argument("relative report paths cannot contain a root name or root directory");
                    }
                    const std::filesystem::path normalized = options.reportPath.lexically_normal();
                    for (const std::filesystem::path &component : normalized)
                    {
                        if (component == "..")
                        {
                            throw std::invalid_argument("relative report paths cannot contain '..'");
                        }
                    }
                    options.reportPath = (outputRoot / normalized).lexically_normal();
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

        /// @brief Appends runner-owned aggregation output to the retained validation report.
        void appendValidationLine(const RunOptions &options, std::string_view line) noexcept
        {
            if (!options.writeReport)
            {
                return;
            }

            try
            {
                const std::filesystem::path parentPath = options.reportPath.parent_path();
                if (!parentPath.empty())
                {
                    std::error_code error;
                    std::filesystem::create_directories(parentPath, error);
                    if (error)
                    {
                        std::cerr << "[VALIDATION] could not create report directory: " << error.message() << '\n';
                        return;
                    }
                }

                std::ofstream report(options.reportPath, std::ios::out | std::ios::app);
                if (!report.is_open())
                {
                    std::cerr << "[VALIDATION] could not append to report: " << options.reportPath.string() << '\n';
                    return;
                }
                report << line << '\n';
                if (!report)
                {
                    std::cerr << "[VALIDATION] report append failed: " << options.reportPath.string() << '\n';
                }
            }
            catch (const std::exception &exception)
            {
                std::cerr << "[VALIDATION] report append failed: " << exception.what() << '\n';
            }
            catch (...)
            {
                std::cerr << "[VALIDATION] report append failed with an unknown error.\n";
            }
        }

        /// @brief Copies static registrations into deterministic order for child routing and execution.
        [[nodiscard]] std::vector<Module> sortedModules(std::span<const Module> registrations)
        {
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

        /// @brief Returns whether one argument belongs to a reserved validation child namespace.
        [[nodiscard]] bool isReservedChildArgument(std::string_view argument) noexcept
        {
            return argument.starts_with("--assert-test-child=") || argument.starts_with("--test-support-test-child=");
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

    TestResult Detail::runWithModules(int argc, char **argv, RunOptions options, std::span<const Module> registrations)
    {
        const ProcessArguments arguments = processArguments(argc, argv);
        const std::vector<Module> modules = sortedModules(registrations);
        if (!validModules(modules))
        {
            return {.modulesFailed = 1, .exitCode = 1};
        }

        // Validation owns disposable process activity beneath its executable's
        // preset tree. Scoped restoration prevents this policy from leaking to
        // the game runtime that may follow embedded startup validation.
        const std::filesystem::path outputRoot = executableDirectory(arguments);
        const std::filesystem::path temporaryRoot = outputRoot / "temp";
        std::error_code temporaryError;
        std::filesystem::create_directories(temporaryRoot, temporaryError);
        if (temporaryError)
        {
            std::cerr << "Validation temporary directory setup failed: " << temporaryError.message() << '\n';
            return {.modulesFailed = 1, .exitCode = 1};
        }
        const std::string temporaryRootText = temporaryRoot.string();
        TestSupport::ScopedEnvironmentVariable temporaryDirectory("TEMP", temporaryRootText);
        TestSupport::ScopedEnvironmentVariable temporaryDirectoryAlias("TMP", temporaryRootText);
        TestSupport::ScopedEnvironmentVariable posixTemporaryDirectory("TMPDIR", temporaryRootText);
        if (!temporaryDirectory.status().ok() || !temporaryDirectoryAlias.status().ok() || !posixTemporaryDirectory.status().ok())
        {
            std::cerr << "Validation temporary environment setup failed.\n";
            return {.modulesFailed = 1, .exitCode = 1};
        }

        // Child-process scenarios use module-owned arguments for crash tests,
        // fatal-path checks, and subprocess helpers. Route them before normal
        // selection so a child process cannot recursively run the full suite.
        const Module *childOwner = nullptr;
        std::size_t reservedChildArguments = 0;
        for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if (value != nullptr && isReservedChildArgument(value))
            {
                ++reservedChildArguments;
            }
        }
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
                // Child routes must be exclusive; otherwise two modules could
                // disagree about the expected protocol and exit-code meaning.
                if (childOwner != nullptr)
                {
                    std::cerr << "Ambiguous validation child invocation matched modules '" << childOwner->name << "' and '" << module.name << "'.\n";
                    return {.modulesFailed = 1, .exitCode = 1, .handledChildInvocation = true};
                }
                childOwner = &module;
            }
        }

        if (reservedChildArguments > 1)
        {
            std::cerr << "Validation child invocation must contain exactly one reserved child selector.\n";
            return {.modulesFailed = 1, .exitCode = 1, .handledChildInvocation = true};
        }
        if (reservedChildArguments == 1 && childOwner == nullptr)
        {
            std::cerr << "Unknown or malformed validation child selector.\n";
            return {.modulesFailed = 1, .exitCode = 1, .handledChildInvocation = true};
        }

        if (childOwner != nullptr)
        {
            // Preserve the owning module's exact exit code for the parent test
            // that launched this child process.
            const int exitCode = invokeModule(*childOwner, {argc, argv, options, false});
            return {
                .modulesRun = 1,
                .modulesFailed = exitCode == 0 ? 0u : 1u,
                .exitCode = exitCode,
                .handledChildInvocation = true,
            };
        }

        const Selection selection = applyArguments(arguments, options);
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
        for (const std::string &excludedModule : selection.excludedModules)
        {
            if (std::ranges::none_of(
                    modules,
                    [&](const Module &module)
                    {
                        return module.name == excludedModule;
                    }))
            {
                std::cerr << "Unknown skipped validation module: " << excludedModule << '\n';
                return {.modulesFailed = 1, .exitCode = 1};
            }
        }
        if (selection.module && selection.excludedModules.contains(*selection.module))
        {
            std::cerr << "Selected validation module is also excluded: " << *selection.module << '\n';
            return {.modulesFailed = 1, .exitCode = 1};
        }

        // Report validation is intentionally non-fatal: invalid report paths
        // disable retained output but should not hide console failures.
        resolveReportOutput(outputRoot, options);
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
            const std::string moduleLine =
                std::format("[VALIDATION] module={} result={} exitCode={}", module.name, moduleExitCode == 0 ? "PASS" : "FAIL", moduleExitCode);
            std::cout << moduleLine << '\n';
            appendValidationLine(options, moduleLine);

            // Once one module has written the aggregate report, later modules
            // must append so their summaries do not replace earlier evidence.
            appendReport = appendReport || options.writeReport;
        }

        if (result.modulesRun == 0)
        {
            std::cerr << "No validation modules were selected.\n";
            result.modulesFailed = 1;
            result.exitCode = 1;
        }

        const std::string resultLine =
            std::format("[VALIDATION] result={} modules={} failed={}", result.ok() ? "PASS" : "FAIL", result.modulesRun, result.modulesFailed);
        std::cout << resultLine << '\n';
        appendValidationLine(options, resultLine);
        return result;
    }

    bool requestsRun(int argc, char **argv) noexcept
    {
        const ProcessArguments arguments = processArguments(argc, argv);
        for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if (value == nullptr)
            {
                continue;
            }
            const std::string_view argument(value);
            if (argument == "--startup-tests" || isReservedChildArgument(argument))
            {
                return true;
            }
        }

        // Some modules own child protocols outside the runner-reserved
        // namespaces (for example Logger and Terminal subprocess scenarios).
        // Embedded validation must route those invocations back into the test
        // runner or the child would accidentally start the game runtime.
        for (const Module &module : registeredModules())
        {
            if (module.handlesChildArguments == nullptr)
            {
                continue;
            }
            try
            {
                if (module.handlesChildArguments(argc, argv))
                {
                    return true;
                }
            }
            catch (...)
            {
                // Enter the runner so its normal exception-to-diagnostic path
                // reports the broken matcher instead of starting the game.
                return true;
            }
        }
        return false;
    }

    TestResult run(int argc, char **argv, RunOptions options)
    {
        return Detail::runWithModules(argc, argv, std::move(options), registeredModules());
    }
} // namespace GameWIP::Validation::Tests
