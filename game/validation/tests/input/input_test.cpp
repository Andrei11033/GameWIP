/// @file input_test.cpp
/// @brief Deterministic Input metadata and HID normalization checks.

#include "validation/tests/input/input_test.h"

#include "test_support/test_support.h"

#if defined(_WIN32)
#include "input/platform/win32/win32_input.h"
#endif

#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace GameWIP::Test
{
    int runInputTests()
    {
        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.writeReport = false;
        TestSupport::Runner runner(reportOptions);

#if defined(_WIN32) && INPUT_INTERNAL_TEST_HOOKS
        runner.runSuite(
            "Input Win32 Unicode device metadata",
            [](TestSupport::Context &context)
            {
                namespace Hooks = GameWIP::Input::Platform::Win32::TestHooks;
                static_cast<void>(context.expectEq("ASCII metadata", std::string{"GameWIP"}, Hooks::convertDeviceMetadata(L"GameWIP")));
                static_cast<void>(
                    context.expectEq("BMP metadata", std::string{"M\xC3\xBCnchen"}, Hooks::convertDeviceMetadata(std::wstring{L"M\u00FCnchen"})));
                static_cast<void>(context.expectEq(
                    "supplementary metadata",
                    std::string{"face \xF0\x9F\x98\x80"},
                    Hooks::convertDeviceMetadata(std::wstring{L"face \xD83D\xDE00"})));
                static_cast<void>(context.expectEq(
                    "lone high surrogate metadata is empty",
                    std::string{},
                    Hooks::convertDeviceMetadata(std::wstring{L"bad \xD83D"})));
                static_cast<void>(context.expectEq(
                    "lone low surrogate metadata is empty",
                    std::string{},
                    Hooks::convertDeviceMetadata(std::wstring{L"bad \xDE00"})));
                static_cast<void>(context.expectEq("empty metadata", std::string{}, Hooks::convertDeviceMetadata(std::wstring{})));
            });

        runner.runSuite(
            "Input Win32 HID normalization",
            [](TestSupport::Context &context)
            {
                namespace Hooks = GameWIP::Input::Platform::Win32::TestHooks;
                using GameWIP::Input::InputDeviceType;

                const auto expectNear = [&context](std::string_view name, float actual, float expected, float tolerance = 0.0001F)
                {
                    static_cast<void>(context.expectTrue(std::string(name), std::fabs(actual - expected) <= tolerance));
                };

                expectNear("non-centered minimum", Hooks::normalizeHidValue(0, 0, 100, InputDeviceType::Joystick, 0x30), 0.0F);
                expectNear("non-centered maximum", Hooks::normalizeHidValue(100, 0, 100, InputDeviceType::Joystick, 0x30), 1.0F);
                expectNear("non-centered center", Hooks::normalizeHidValue(50, 0, 100, InputDeviceType::Joystick, 0x30), 0.5F);
                expectNear(
                    "LONG_MIN centered minimum",
                    Hooks::normalizeHidValue(
                        (std::numeric_limits<long>::min)(),
                        (std::numeric_limits<long>::min)(),
                        (std::numeric_limits<long>::max)(),
                        InputDeviceType::Joystick,
                        0x30),
                    -1.0F);
                expectNear(
                    "LONG_MAX centered maximum",
                    Hooks::normalizeHidValue(
                        (std::numeric_limits<long>::max)(),
                        (std::numeric_limits<long>::min)(),
                        (std::numeric_limits<long>::max)(),
                        InputDeviceType::Joystick,
                        0x30),
                    1.0F);
                expectNear("centered gamepad minimum", Hooks::normalizeHidValue(-32768, -32768, 32767, InputDeviceType::Gamepad, 0x30), -1.0F);
                expectNear("centered gamepad maximum", Hooks::normalizeHidValue(32767, -32768, 32767, InputDeviceType::Gamepad, 0x30), 1.0F);
                expectNear("centered gamepad center", Hooks::normalizeHidValue(0, -32768, 32767, InputDeviceType::Gamepad, 0x30), 0.0F, 0.0002F);
                expectNear(
                    "inverted centered gamepad",
                    Hooks::normalizeHidValue(16384, -32768, 32767, InputDeviceType::Gamepad, 0x31),
                    -0.5F,
                    0.0002F);
                static_cast<void>(
                    context.expectTrue("invalid HID range is neutral", Hooks::normalizeHidValue(1, 2, 2, InputDeviceType::Joystick, 0x30) == 0.0F));
            });
#else
        runner.runSuite(
            "Input Win32 hooks skipped",
            [](TestSupport::Context &context)
            {
                context.pass("Input Win32 hooks are unavailable on this build");
            });
#endif

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Input checks passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
