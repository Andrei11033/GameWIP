/// @file input.cpp
/// @brief Win32 Terminal input validation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef TERMINAL_INTERNAL_TEST_HOOKS
#define TERMINAL_INTERNAL_TEST_HOOKS 0
#endif

#include "validation/tests/terminal/platform.h"

#include "validation/executable_path.h"

#include "terminal/terminal.h"
#include "test_support/test_support.h"

#if TERMINAL_INTERNAL_TEST_HOOKS
#include "terminal/internal/terminal_test_hooks.h"
#include "terminal/platform/win32/internal/win32_terminal_test_hooks.h"
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#include <windows.h>

namespace GameWIP::Test::TerminalPlatform
{
#if TERMINAL_INTERNAL_TEST_HOOKS
    namespace
    {
        namespace Terminal = GameWIP::Terminal;
        namespace TestSupport = GameWIP::TestSupport;
        namespace Hooks = GameWIP::Terminal::TestHooks;
        namespace PlatformHooks = GameWIP::Terminal::Detail::Platform::TestHooks;

        using GameWIP::IO::Types::ErrorCode;

#include "validation/tests/terminal/platform/win32/key_decoder_test.inl"
#include "validation/tests/terminal/platform/win32/endpoint_test.inl"
#include "validation/tests/terminal/platform/win32/cancellation_test.inl"
    } // namespace
#endif

    /// ------------------------------------------------------------
    /// Native suite registration
    /// ------------------------------------------------------------

    bool handlesCancellationSignalFailureChild(std::string_view argument) noexcept
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        return argument == kCancellationSignalFailureChildArgument;
#else
        static_cast<void>(argument);
        return false;
#endif
    }

    void registerWin32Suites(TestSupport::Runner &runner)
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        runner.runSuite("Terminal Win32 event decoder", testWin32EventDecoder);
        runner.runSuite("Terminal stdin endpoint replacement", testInputEndpointReplacement);
        runner.runSuite(
            "Terminal cancellation signal failure",
            [](TestSupport::Context &context)
            {
                testCancellationSignalFailure(context, GameWIP::Validation::currentExecutablePath());
            });
        runner.runSuite("Terminal cancellation reset failure", testCancellationResetFailure);
#else
        static_cast<void>(runner);
#endif
    }

    int runCancellationSignalFailureChild()
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        return runCancellationSignalFailureChildMode();
#else
        return 2;
#endif
    }
} // namespace GameWIP::Test::TerminalPlatform
