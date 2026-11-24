/// @file child_arguments.cpp
/// @brief Win32 Desktop child-process routing.

#include "validation/tests/desktop/platform.h"

#include "validation/process_arguments.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

#ifndef DESKTOP_INTERNAL_TEST_HOOKS
#define DESKTOP_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Test::DesktopPlatform
{
    bool handlesChildArguments(int argc, char **argv)
    {
        constexpr std::array selectors{
            std::string_view{"--desktop-test-child=result-empty-arrow"},
            std::string_view{"--desktop-test-child=result-failed-dereference"},
            std::string_view{"--desktop-test-child=accessibility-client"},
            std::string_view{"--desktop-test-child=standalone-color-shutdown"},
            std::string_view{"--desktop-test-child=window-color-shutdown"},
            std::string_view{"--desktop-test-child=owner-exit-color-shutdown"},
            std::string_view{"--desktop-test-child=owner-exit-drag-drop-shutdown"},
            std::string_view{"--desktop-test-child=owner-exit-drag-drop-revocation-failure"},
            std::string_view{"--desktop-test-child=owner-exit-progress-shutdown"},
            std::string_view{"--desktop-test-child=owner-exit-progress-close-failure"},
#if DESKTOP_INTERNAL_TEST_HOOKS
            std::string_view{"--desktop-test-child=deferred-pump-failure-wait"},
            std::string_view{"--desktop-test-child=cold-com-apartment"},
            std::string_view{"--desktop-test-child=jump-list-publication"},
            std::string_view{"--desktop-test-child=progress-restore-message-laziness"},
#endif
            std::string_view{"--desktop-test-child=shell-launch-probe"},
        };

        const auto arguments = GameWIP::Validation::processArguments(argc, argv);
        return std::ranges::any_of(
            arguments.subspan(std::min<std::size_t>(1, arguments.size())),
            [&](const char *value)
            {
                return value != nullptr && std::ranges::find(selectors, std::string_view(value)) != selectors.end();
            });
    }
} // namespace GameWIP::Test::DesktopPlatform
