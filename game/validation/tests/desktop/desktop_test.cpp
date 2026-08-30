/// @file desktop_test.cpp
/// @brief Deterministic Desktop checks and opt-in visible manual validation.

#include "validation/tests/desktop/desktop_test.h"
#include "validation/process_arguments.h"

#include "test_support/test_support.h"
#include "desktop/child_surface.h"
#include "desktop/clipboard.h"
#include "desktop/cursor.h"
#include "desktop/data_transfer.h"
#include "desktop/internal/cursor_selection.h"
#include "desktop/native/win32.h"
#include "desktop/renderer_bridge.h"
#include "desktop/window.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#ifndef DESKTOP_INTERNAL_TEST_HOOKS
#define DESKTOP_INTERNAL_TEST_HOOKS 0
#endif

#if DESKTOP_INTERNAL_TEST_HOOKS
#include "desktop/internal/desktop_test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
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
    using ErrorCode = IO::Types::ErrorCode;

    inline constexpr wchar_t kManualTestAppUserModelId[] = L"GameWIP.Validation.DesktopManualTests";

    static_assert(!std::is_move_constructible_v<Desktop::Window>);
    static_assert(!std::is_move_assignable_v<Desktop::Window>);
    static_assert(!std::is_copy_constructible_v<Desktop::Window>);
    static_assert(!std::is_copy_assignable_v<Desktop::Window>);
    static_assert(!std::is_move_constructible_v<Desktop::ChildSurface>);
    static_assert(!std::is_move_assignable_v<Desktop::ChildSurface>);
    static_assert(!std::is_copy_constructible_v<Desktop::ChildSurface>);
    static_assert(!std::is_copy_assignable_v<Desktop::ChildSurface>);
    static_assert(noexcept(Desktop::Events::poll()));
    static_assert(noexcept(Desktop::Events::wait()));
    static_assert(noexcept(std::declval<Desktop::Window &>().close()));
    static_assert(noexcept(Desktop::Display::getColorInfo(Desktop::Types::Display::MonitorId{})));
    static_assert(noexcept(Desktop::Display::getColorInfo(std::declval<const Desktop::Window &>())));
    static_assert(std::is_same_v<decltype(Desktop::Types::Events::FilesDropped{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);

#include "validation/tests/desktop/desktop_manual_tests.inl"
#include "validation/tests/desktop/desktop_lifecycle_tests.inl"
#include "validation/tests/desktop/desktop_event_tests.inl"
#include "validation/tests/desktop/desktop_renderer_tests.inl"
#include "validation/tests/desktop/desktop_display_tests.inl"
#include "validation/tests/desktop/desktop_cursor_tests.inl"
#include "validation/tests/desktop/desktop_child_surface_tests.inl"
#include "validation/tests/desktop/desktop_clipboard_tests.inl"
} // namespace

namespace GameWIP::Test
{
    int runDesktopTests(int argc, char **argv, const DesktopTestOptions &options)
    {
        const HRESULT manualShellIdentityStatus =
            options.enableManualTests ? SetCurrentProcessExplicitAppUserModelID(kManualTestAppUserModelId) : S_OK;
        std::optional<std::string_view> selectedManualSuite;
        constexpr std::string_view manualSuitePrefix = "--desktop-manual-suite=";
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);
        for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if (value != nullptr && std::string_view(value).starts_with(manualSuitePrefix))
                selectedManualSuite = std::string_view(value).substr(manualSuitePrefix.size());
        }
        constexpr std::array manualSuiteNames{
            std::string_view{"lifecycle"},
            std::string_view{"multiple-windows"},
            std::string_view{"custom-chrome"},
            std::string_view{"layered-pointer"},
            std::string_view{"dpi"},
            std::string_view{"cursor"},
            std::string_view{"child-surface"},
            std::string_view{"files-shell"},
            std::string_view{"fullscreen"},
            std::string_view{"borderless"},
            std::string_view{"exclusive"},
            std::string_view{"topology"},
            std::string_view{"hdr"},
            std::string_view{"modern"}};

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.flushReportEachLine = options.enableManualTests;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        ManualStatusWindow statusWindow(options.enableManualTests);
        manualStatusWindow = &statusWindow;
        if (options.enableManualTests)
        {
            runner.runSuite(
                "Window manual shell identity",
                [&](TestSupport::Context &context)
                {
                    static_cast<void>(context.expectTrue("manual test AppUserModelID applies", SUCCEEDED(manualShellIdentityStatus)));
                });
        }
        runner.runSuite("Window passive values and closed state", testPassiveValuesAndClosedState);
        runner.runSuite("Window Clipboard values and validation", testClipboardValuesAndValidation);
        runner.runSuite("Window Clipboard native round trips", testClipboardRoundTrips);
        runner.runSuite("Window Clipboard multi-format and failure semantics", testClipboardMultiFormatAndFailures);
        runner.runSuite("Window description validation", testDescriptionValidation);
        runner.runSuite("Window cursor DPI selection", testCursorDpiSelection);
        runner.runSuite("Window custom cursor values and validation", testCursorValuesAndValidation);
#if DESKTOP_INTERNAL_TEST_HOOKS
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
        runner.runSuite("Window renderer occlusion feedback", testRendererOcclusionFeedback);
        runner.runSuite("Window display color information", testDisplayColorInformation);
        runner.runSuite("Window monitors and display modes", testMonitors);
        const bool validManualSelection = !selectedManualSuite || std::ranges::find(manualSuiteNames, *selectedManualSuite) != manualSuiteNames.end();
        if (!validManualSelection)
        {
            runner.runSuite(
                "Window manual suite selection",
                [&](TestSupport::Context &context)
                {
                    context.fail("Window manual suite selector", std::format("unknown suite '{}'", *selectedManualSuite));
                });
        }
        const auto runManualSuite = [&](std::string_view displayName, std::string_view selector, auto suite)
        {
            if (!validManualSelection || (selectedManualSuite && *selectedManualSuite != selector))
                return;
            runner.runSuite(
                displayName,
                [&](TestSupport::Context &context)
                {
                    suite(context, options);
                });
        };
        const auto runSelectedManualSuite = [&](std::string_view displayName, std::string_view selector, auto suite)
        {
            if (!validManualSelection || !selectedManualSuite || *selectedManualSuite != selector)
                return;
            runner.runSuite(
                displayName,
                [&](TestSupport::Context &context)
                {
                    suite(context, options);
                });
        };
        runManualSuite("Window manual visible lifecycle", "lifecycle", testManualVisibleLifecycle);
        runManualSuite("Window manual multiple windows", "multiple-windows", testManualMultipleWindows);
        runManualSuite("Window manual custom chrome", "custom-chrome", testManualCustomChrome);
        runManualSuite("Window manual layered and pointer behavior", "layered-pointer", testManualLayeredAndPointer);
        runManualSuite("Window manual DPI and coordinates", "dpi", testManualDpiAndCoordinates);
        runManualSuite("Window manual cursor behavior", "cursor", testManualCursor);
        runManualSuite("Window manual native child surface", "child-surface", testManualChildSurface);
        runManualSuite("Window manual files and shell behavior", "files-shell", testManualFilesAndShell);
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

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Desktop library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        manualStatusWindow = nullptr;
        return runner.exitCode();
    }
} // namespace GameWIP::Test
