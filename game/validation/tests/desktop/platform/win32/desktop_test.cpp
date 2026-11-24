/// @file desktop_test.cpp
/// @brief Deterministic Desktop checks and opt-in visible manual validation.

#include "validation/tests/desktop/desktop_test.h"

#include "validation/executable_path.h"
#include "validation/tests/desktop/accessibility_test.h"
#include "validation/tests/internal/path_text.h"
#include "validation/process_arguments.h"
#include "validation/tests/internal/helpers/runner_helpers.h"

#ifndef DESKTOP_INTERNAL_TEST_HOOKS
#define DESKTOP_INTERNAL_TEST_HOOKS 0
#endif

#include "desktop/accessibility.h"
#include "desktop/child_surface.h"
#include "desktop/clipboard.h"
#include "desktop/cursor.h"
#include "desktop/data_transfer.h"
#include "desktop/dialogs.h"
#include "desktop/drag_drop.h"
#include "desktop/internal/cursor_selection.h"
#if DESKTOP_INTERNAL_TEST_HOOKS
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/test_hooks.h"
#endif
#include "desktop/native/win32.h"
#include "desktop/renderer_bridge.h"
#include "desktop/shell.h"
#include "desktop/shell_jump_lists.h"
#include "desktop/shell_notifications.h"
#include "desktop/shell_registration.h"
#include "desktop/shell_taskbar.h"
#include "desktop/shell_tray.h"
#include "desktop/shell_types.h"
#include "desktop/window.h"
#include "filesystem/file.h"
#include "test_support/test_support.h"
#include "unicode/unicode.h"

#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;
    namespace Desktop = GameWIP::Desktop;
    namespace Win32TestHooks = Desktop::Internal::TestHooks::Win32;
    namespace ShellTypes = Desktop::Types::Shell;
    namespace TaskbarTypes = Desktop::Types::Taskbar;
    namespace TrayTypes = Desktop::Types::Tray;
    namespace NotificationTypes = Desktop::Types::Notifications;
    namespace JumpListTypes = Desktop::Types::JumpLists;
    namespace RegistrationTypes = Desktop::Types::Registration;
    using IO::Types::ErrorCode;

#include "validation/tests/desktop/platform/win32/desktop_process_tests.inl"

    /// ------------------------------------------------------------
    /// Manual scenario support
    /// ------------------------------------------------------------

#include "validation/tests/desktop/platform/win32/desktop_manual_fullscreen_support.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_status.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_transfer_support.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_surface.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_interaction.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_window_setup.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_drag_drop_support.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_shell_launch_support.inl"

    /// ------------------------------------------------------------
    /// Manual scenario tests
    /// ------------------------------------------------------------

#include "validation/tests/desktop/platform/win32/desktop_manual_lifecycle_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_chrome_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_dpi_cursor_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_accessibility_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_file_drop_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_shell_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_window_policy_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_files_shell_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_drag_drop_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_fullscreen_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_color_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_modern_capability_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_child_surface_tests.inl"

    /// ------------------------------------------------------------
    /// Dialog validation
    /// ------------------------------------------------------------

#include "validation/tests/desktop/platform/win32/desktop_file_dialog_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_apartment_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_message_dialog_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_prompt_dialog_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_progress_dialog_support.inl"
#include "validation/tests/desktop/platform/win32/desktop_progress_dialog_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_progress_dialog_ownership_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_focused_dialog_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_dialog_tests.inl"

    /// ------------------------------------------------------------
    /// Window contract tests
    /// ------------------------------------------------------------

#include "validation/tests/desktop/platform/win32/desktop_values_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_lifecycle_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_native_window_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_event_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_child_surface_tests.inl"

    /// ------------------------------------------------------------
    /// Renderer, display, and cursor tests
    /// ------------------------------------------------------------

#include "validation/tests/desktop/platform/win32/desktop_presentation_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_pointer_hit_mask_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_renderer_occlusion_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_display_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_cursor_support.inl"
#include "validation/tests/desktop/platform/win32/desktop_cursor_selection_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_cursor_native_tests.inl"

    /// ------------------------------------------------------------
    /// Clipboard, drag-and-drop, and shell tests
    /// ------------------------------------------------------------

#include "validation/tests/desktop/platform/win32/desktop_clipboard_support.inl"
#include "validation/tests/desktop/platform/win32/desktop_manual_clipboard_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_clipboard_contract_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_clipboard_roundtrip_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_clipboard_failure_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_drag_drop_contract_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_drag_drop_target_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_drag_drop_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_shell_event_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_shell_lifecycle_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_jump_list_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_shell_registration_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_shell_application_registration_tests.inl"
#include "validation/tests/desktop/platform/win32/desktop_shell_launch_tests.inl"
} // namespace

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Desktop suite registration
    /// ------------------------------------------------------------

    int runDesktopTests(int argc, char **argv, const DesktopTestOptions &options)
    {
        // Dispatch child-process modes before creating the suite runner.
        if(GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-test-child=result-empty-arrow"))
        {
            std::set_terminate(
                []
                {
                    std::_Exit(73);
                });
            const Desktop::Types::Result<Desktop::Types::LogicalSize> result{IO::successStatus(), std::nullopt};
            return static_cast<int>(result->width);
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-test-child=result-failed-dereference"))
        {
            std::set_terminate(
                []
                {
                    std::_Exit(73);
                });
            Desktop::Types::Result<Desktop::Types::LogicalSize> result{IO::makeStatus(ErrorCode::NativeFailure), Desktop::Types::LogicalSize{1, 2}};
            return static_cast<int>((*result).width);
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-test-child=accessibility-client"))
        {
            return runAccessibilityClientChild();
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kStandaloneColorChildArgument))
        {
            return runStandaloneColorShutdownChild();
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kWindowColorChildArgument))
        {
            return runWindowColorShutdownChild();
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kOwnerExitColorChildArgument))
        {
            return runOwnerExitColorShutdownChild();
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kOwnerExitDragDropChildArgument))
        {
            return runOwnerExitDragDropShutdownChild(false);
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kOwnerExitDragDropFailureChildArgument))
        {
            return runOwnerExitDragDropShutdownChild(true);
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kOwnerExitProgressChildArgument))
        {
            return runOwnerExitProgressShutdownChild(false);
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kOwnerExitProgressFailureChildArgument))
        {
            return runOwnerExitProgressShutdownChild(true);
        }

#if DESKTOP_INTERNAL_TEST_HOOKS
        if(GameWIP::Validation::hasProcessArgument(argc, argv, kDeferredPumpFailureWaitChildArgument))
        {
            return runDeferredPumpFailureWaitChild();
        }
#endif

#if DESKTOP_INTERNAL_TEST_HOOKS
        if(GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-test-child=cold-com-apartment"))
        {
            return runColdApartmentChild();
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-test-child=jump-list-publication"))
        {
            return runJumpListPublicationChild(GameWIP::Validation::currentExecutablePath());
        }

        if(GameWIP::Validation::hasProcessArgument(argc, argv, kProgressRestoreMessageLazinessChildArgument))
        {
            return runProgressRestoreMessageLazinessChild();
        }

#endif

        if(GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-test-child=shell-launch-probe"))
        {
            return runShellLaunchProbeChild(argc, argv);
        }

        // Resolve the optional manual-suite selection before suite registration.
        const HRESULT manualShellIdentityStatus =
            options.enableManualTests ? SetCurrentProcessExplicitAppUserModelID(kManualTestAppUserModelId) : S_OK;
        std::optional<std::string_view> selectedManualSuite;
        manualCaseSelection.reset();
        manualCasesSelected = 0;
        const bool manualOnly = GameWIP::Validation::hasProcessArgument(argc, argv, "--desktop-manual-only");
        constexpr std::string_view manualSuitePrefix = "--desktop-manual-suite=";
        constexpr std::string_view manualCasePrefix = "--desktop-manual-case=";
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);

        for(const char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if(value != nullptr && std::string_view(value).starts_with(manualSuitePrefix))
            {
                selectedManualSuite = std::string_view(value).substr(manualSuitePrefix.size());
            }

            if(value != nullptr && std::string_view(value).starts_with(manualCasePrefix))
            {
                manualCaseSelection = std::string_view(value).substr(manualCasePrefix.size());
            }
        }

        constexpr std::array manualSuiteNames{
            std::string_view{"lifecycle"},     std::string_view{"accessibility"},   std::string_view{"multiple-windows"},
            std::string_view{"custom-chrome"}, std::string_view{"layered-pointer"}, std::string_view{"dpi"},
            std::string_view{"cursor"},        std::string_view{"child-surface"},   std::string_view{"files-shell"},
            std::string_view{"shell"},         std::string_view{"files-window"},    std::string_view{"clipboard"},
            std::string_view{"drag-drop"},     std::string_view{"dialogs"},         std::string_view{"fullscreen"},
            std::string_view{"borderless"},    std::string_view{"exclusive"},       std::string_view{"topology"},
            std::string_view{"hdr"},           std::string_view{"modern"}};

        TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);
        reportOptions.flushReportEachLine = options.enableManualTests;

        TestSupport::Runner runner(reportOptions);
        ManualStatusWindow statusWindow(options.enableManualTests);
        manualObservationCounts = {};
        manualStatusWindow = &statusWindow;

        if(options.enableManualTests)
        {
            runner.runSuite(
                "Window manual shell identity",
                [&](TestSupport::Context &context)
                {
                    static_cast<void>(context.expectTrue("manual test AppUserModelID applies", SUCCEEDED(manualShellIdentityStatus)));
                });
        }

        if(manualOnly && !options.enableManualTests)
        {
            runner.runSuite(
                "Desktop manual selection",
                [](TestSupport::Context &context)
                {
                    context.fail("manual-only requires --manual-tests", "enable manual tests explicitly before omitting automated checks");
                });
        }

        // Register automated suites unless only manual checks were requested.
        if(!manualOnly)
        {
            runner.runSuite("Window passive values and closed state", testPassiveValuesAndClosedState);
            runner.runSuite("Desktop result access preconditions", testResultAccessPreconditions);
            runAccessibilityTests(runner);
            runner.runSuite("Window Clipboard values and validation", testClipboardValuesAndValidation);
            runner.runSuite("Window Clipboard native round trips", testClipboardRoundTrips);
            runner.runSuite("Window Clipboard multi-format and failure semantics", testClipboardMultiFormatAndFailures);
            runner.runSuite("Desktop clipboard fixture contention", testClipboardFixtureContention);
            runner.runSuite("Window native data drag and drop", testDragDrop);
            runner.runSuite("Window description validation", testDescriptionValidation);
            runner.runSuite("Window native construction lifecycle", testNativeConstructionLifecycle);
            runner.runSuite("Window initial monitor geometry", testInitialMonitorGeometry);
            runner.runSuite("Window cursor DPI selection", testCursorDpiSelection);
            runner.runSuite("Window custom cursor values and validation", testCursorValuesAndValidation);
#if DESKTOP_INTERNAL_TEST_HOOKS
            runner.runSuite("Desktop file and folder dialogs", testFileAndFolderDialogs);
            runner.runSuite(
                "Desktop dialog and shell COM apartments",
                [&](TestSupport::Context &context)
                {
                    testDialogApartmentContracts(context, GameWIP::Validation::currentExecutablePath());
                });
            runner.runSuite("Desktop fixed-button messages", testMessageDialogs);
            runner.runSuite("Desktop semantic prompts", testPromptDialogs);
            runner.runSuite("Desktop ProgressDialog validation", testProgressDialogValidation);
            runner.runSuite("Desktop ProgressDialog native lifecycle", testProgressDialogNativeLifecycle);
            runner.runSuite("Desktop ProgressDialog ownership", testProgressDialogOwnership);
            runner.runSuite("Desktop ProgressDialog exceptional lifetime", testProgressDialogExceptionalLifetime);
            runner.runSuite("Window custom cursor native resources", testCursorNativeResources);
            runner.runSuite("Window custom cursor integration", testCursorWindowIntegration);
            runner.runSuite("Window custom cursor lifecycle", testCursorLifecycle);
            runner.runSuite("Window fixed event queue", testFixedEventQueue);
            runner.runSuite("Window sticky close", testStickyClose);
            runner.runSuite("Window deterministic failure paths", testFailureInjection);
            runner.runSuite("Window threading contracts", testThreadingContracts);
            runner.runSuite("Window exceptional lifetime", testExceptionalLifetime);
            runner.runSuite("Window cross-thread presentation publication", testPresentationPublication);
            runner.runSuite("Window packed pointer hit mask", testPointerHitMask);
#else
            runner.runSuite(
                "Window fixed event queue",
                [](TestSupport::Context &context)
                {
                    context.skip("Desktop queue hooks", "DESKTOP_INTERNAL_TEST_HOOKS is disabled");
                });
#endif
            runner.runSuite("Window hidden native lifecycle", testHiddenNativeWindow);
            runner.runSuite("Window native child surfaces", testChildSurfaces);
            runner.runSuite("Window native event translation", testNativeEventTranslation);
            runner.runSuite("Desktop shell queue and capabilities", testShellQueueAndCapabilities);
            runner.runSuite("Desktop shell pumping without Window", testShellPumpingWithoutWindow);
            runner.runSuite("Desktop shell validation", testShellValidation);
            runner.runSuite("Desktop shell native lifetimes", testShellNativeLifetimes);
            runner.runSuite("Desktop tray native callbacks", testTrayNativeCallbacks);
#if DESKTOP_INTERNAL_TEST_HOOKS
            runner.runSuite("Desktop shell failure hooks", testShellFailureHooks);
            runner.runSuite(
                "Desktop jump-list native publication",
                [&](TestSupport::Context &context)
                {
                    testJumpListNativePublication(context, GameWIP::Validation::currentExecutablePath());
                });
            runner.runSuite(
                "Desktop shell registration ownership",
                [&](TestSupport::Context &context)
                {
                    testShellRegistrationOwnership(context, GameWIP::Validation::currentExecutablePath());
                });
            runner.runSuite(
                "Desktop shell registration application ownership",
                [&](TestSupport::Context &context)
                {
                    testShellRegistrationApplicationOwnership(context, GameWIP::Validation::currentExecutablePath());
                });
            runner.runSuite(
                "Desktop shell association discovery and launch",
                [&](TestSupport::Context &context)
                {
                    testShellRegistrationLaunch(context, GameWIP::Validation::currentExecutablePath());
                });
#endif
            runner.runSuite("Window renderer occlusion feedback", testRendererOcclusionFeedback);
            runner.runSuite("Window display color information", testDisplayColorInformation);
            runner.runSuite(
                "Window process-isolated shutdown",
                [&](TestSupport::Context &context)
                {
                    testDesktopProcessShutdown(context, GameWIP::Validation::currentExecutablePath());
                });
            runner.runSuite("Window monitors and display modes", testMonitors);
        }

        // Register the manual suites selected by the command line.
        const bool validManualSelection = !selectedManualSuite || std::ranges::find(manualSuiteNames, *selectedManualSuite) != manualSuiteNames.end();
        if(!validManualSelection)
        {
            runner.runSuite(
                "Window manual suite selection",
                [&](TestSupport::Context &context)
                {
                    context.fail("Window manual suite selector", std::format("unknown suite '{}'", *selectedManualSuite));
                });
        }

        // Track manual suite names for consolidated skip reporting.
        std::vector<std::string_view> pendingManualSkips;

        const auto registerManualSuite = [&](std::string_view displayName, auto suite)
        {
            if(!options.enableManualTests)
            {
                pendingManualSkips.push_back(displayName);
                return;
            }

            runner.runSuite(
                displayName,
                [&](TestSupport::Context &context)
                {
                    suite(context, options);
                });
        };

        const auto runManualSuite = [&](std::string_view displayName, std::string_view selector, auto suite)
        {
            if(!validManualSelection || (selectedManualSuite && *selectedManualSuite != selector))
            {
                return;
            }

            registerManualSuite(displayName, suite);
        };

        const auto runSelectedManualSuite = [&](std::string_view displayName, std::string_view selector, auto suite)
        {
            if(!validManualSelection || !selectedManualSuite || *selectedManualSuite != selector)
            {
                return;
            }

            registerManualSuite(displayName, suite);
        };

        runManualSuite("Window manual visible lifecycle", "lifecycle", testManualVisibleLifecycle);
        runManualSuite("Desktop manual accessibility", "accessibility", testManualAccessibility);
        runManualSuite("Window manual multiple windows", "multiple-windows", testManualMultipleWindows);
        runManualSuite("Window manual custom chrome", "custom-chrome", testManualCustomChrome);
        runManualSuite("Window manual layered and pointer behavior", "layered-pointer", testManualLayeredAndPointer);
        runManualSuite("Window manual DPI and coordinates", "dpi", testManualDpiAndCoordinates);
        runManualSuite("Window manual cursor behavior", "cursor", testManualCursor);
        runManualSuite("Window manual native child surface", "child-surface", testManualChildSurface);
        runManualSuite(
            "Window manual files and shell behavior",
            "files-shell",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFilesAndShell(context, manualOptions);
            });
        runSelectedManualSuite(
            "Desktop manual shell integration",
            "shell",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFilesAndShell(context, manualOptions, false, true);
            });
        runSelectedManualSuite(
            "Window manual files and frame behavior",
            "files-window",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFilesAndShell(context, manualOptions, true, false);
            });
        runManualSuite("Desktop manual clipboard interoperability", "clipboard", testManualClipboard);
        runManualSuite("Window manual native data drag and drop", "drag-drop", testManualDragDrop);
        runManualSuite("Desktop manual native dialogs", "dialogs", testManualDialogs);
        runManualSuite(
            "Window manual fullscreen and topology",
            "fullscreen",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFullscreenAndTopology(context, manualOptions, ManualFullscreenSections{});
            });
        runSelectedManualSuite(
            "Window manual borderless fullscreen",
            "borderless",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFullscreenAndTopology(
                    context,
                    manualOptions,
                    ManualFullscreenSections{.borderless = true, .exclusive = false, .topology = false});
            });
        runSelectedManualSuite(
            "Window manual exclusive fullscreen",
            "exclusive",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFullscreenAndTopology(
                    context,
                    manualOptions,
                    ManualFullscreenSections{.borderless = false, .exclusive = true, .topology = false});
            });
        runSelectedManualSuite(
            "Window manual display topology",
            "topology",
            [](TestSupport::Context &context, const DesktopTestOptions &manualOptions)
            {
                testManualFullscreenAndTopology(
                    context,
                    manualOptions,
                    ManualFullscreenSections{.borderless = false, .exclusive = false, .topology = true});
            });
        runManualSuite("Window manual HDR and advanced color", "hdr", testManualHdrAndAdvancedColor);
        runManualSuite("Window manual modern Windows capabilities", "modern", testManualModernWindowsCapabilities);

        // Report skipped or unmatched manual selections.
        if(!pendingManualSkips.empty())
        {
            runner.runSuite(
                "Desktop manual tests",
                [&](TestSupport::Context &context)
                {
                    context.skip("Desktop manual tests", std::format("{} manual tests disabled by DesktopTestOptions", pendingManualSkips.size()));
                });
        }

        if(options.enableManualTests && manualCaseSelection && manualCasesSelected == 0)
        {
            runner.runSuite(
                "Desktop manual case selection",
                [&](TestSupport::Context &context)
                {
                    context.fail("manual case selector matched no scenarios", *manualCaseSelection);
                });
        }

        // Report the final manual observations and suite totals.
        const TestSupport::Types::Reporting::Summary result = runner.result();

        if(options.enableManualTests)
        {
            runner.summary(
                std::format(
                    "Desktop manual observations passed={} failed={} skipped={} (excludes setup and contract assertions)",
                    manualObservationCounts.passed,
                    manualObservationCounts.failed,
                    manualObservationCounts.skipped));
        }
        runner.summary(std::format("Desktop library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        manualStatusWindow = nullptr;
        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
