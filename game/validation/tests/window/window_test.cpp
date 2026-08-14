/// @file window_test.cpp
/// @brief Deterministic Window checks and opt-in visible manual validation.

#include "validation/tests/window/window_test.h"

#include "test_support/test_support.h"
#include "window/renderer_bridge.h"
#include "window/native/win32.h"
#include "window/window.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#ifndef WINDOW_INTERNAL_TEST_HOOKS
#define WINDOW_INTERNAL_TEST_HOOKS 0
#endif

#if WINDOW_INTERNAL_TEST_HOOKS
#include "window/internal/window_test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <limits>
#include <memory>
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
    namespace Window = GameWIP::Window;
    using ErrorCode = IO::Types::ErrorCode;

    inline constexpr wchar_t kManualTestAppUserModelId[] = L"GameWIP.Validation.WindowManualTests";

    static_assert(!std::is_move_constructible_v<Window::Window>);
    static_assert(!std::is_move_assignable_v<Window::Window>);
    static_assert(!std::is_copy_constructible_v<Window::Window>);
    static_assert(!std::is_copy_assignable_v<Window::Window>);
    static_assert(noexcept(Window::Events::poll()));
    static_assert(noexcept(Window::Events::wait()));
    static_assert(noexcept(std::declval<Window::Window &>().close()));
    static_assert(noexcept(Window::Display::getColorInfo(Window::Types::Display::MonitorId{})));
    static_assert(noexcept(Window::Display::getColorInfo(std::declval<const Window::Window &>())));
    static_assert(std::is_same_v<decltype(Window::Types::Events::FilesDropped{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);

#include "validation/tests/window/window_manual_tests.inl"
#include "validation/tests/window/window_lifecycle_tests.inl"
#include "validation/tests/window/window_event_tests.inl"
#include "validation/tests/window/window_renderer_tests.inl"
#include "validation/tests/window/window_display_tests.inl"
} // namespace

namespace GameWIP::Test
{
    int runWindowTests(int argc, char **argv, const WindowTestOptions &options)
    {
        const HRESULT manualShellIdentityStatus =
            options.enableManualTests ? SetCurrentProcessExplicitAppUserModelID(kManualTestAppUserModelId) : S_OK;
        std::optional<std::string_view> selectedManualSuite;
        constexpr std::string_view manualSuitePrefix = "--window-manual-suite=";
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]).starts_with(manualSuitePrefix))
                selectedManualSuite = std::string_view(argv[index]).substr(manualSuitePrefix.size());
        }
        constexpr std::array manualSuiteNames{
            std::string_view{"lifecycle"},
            std::string_view{"multiple-windows"},
            std::string_view{"custom-chrome"},
            std::string_view{"layered-pointer"},
            std::string_view{"dpi"},
            std::string_view{"cursor"},
            std::string_view{"files-shell"},
            std::string_view{"fullscreen"},
            std::string_view{"hdr"},
            std::string_view{"modern"}};

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
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
        runner.runSuite("Window description validation", testDescriptionValidation);
#if WINDOW_INTERNAL_TEST_HOOKS
        runner.runSuite("Window fixed event queue", testFixedEventQueue);
        runner.runSuite("Window sticky close", testStickyClose);
        runner.runSuite("Window deterministic failure paths", testFailureInjection);
        runner.runSuite("Window threading contracts", testThreadingContracts);
        runner.runSuite("Window exceptional lifetime", testExceptionalLifetime);
        runner.runSuite("Window packed pointer hit mask", testPointerHitMask);
#else
        runner.runSuite(
            "Window fixed event queue",
            [](TestSupport::Context &context)
            {
                context.skip("Window queue hooks", "WINDOW_INTERNAL_TEST_HOOKS is disabled");
            });
#endif
        runner.runSuite("Window hidden native lifecycle", testHiddenNativeWindow);
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
        runManualSuite("Window manual visible lifecycle", "lifecycle", testManualVisibleLifecycle);
        runManualSuite("Window manual multiple windows", "multiple-windows", testManualMultipleWindows);
        runManualSuite("Window manual custom chrome", "custom-chrome", testManualCustomChrome);
        runManualSuite("Window manual layered and pointer behavior", "layered-pointer", testManualLayeredAndPointer);
        runManualSuite("Window manual DPI and coordinates", "dpi", testManualDpiAndCoordinates);
        runManualSuite("Window manual cursor behavior", "cursor", testManualCursor);
        runManualSuite("Window manual files and shell behavior", "files-shell", testManualFilesAndShell);
        runManualSuite("Window manual fullscreen and topology", "fullscreen", testManualFullscreenAndTopology);
        runManualSuite("Window manual HDR and advanced color", "hdr", testManualHdrAndAdvancedColor);
        runManualSuite("Window manual modern Windows capabilities", "modern", testManualModernWindowsCapabilities);

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Window library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        manualStatusWindow = nullptr;
        return runner.exitCode();
    }
} // namespace GameWIP::Test
