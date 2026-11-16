/// @file runtime_options.cpp
/// @brief Parses runtime logging, Input, and window-loop settings.

#include "runtime_options.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <system_error>

namespace
{
    /// ------------------------------------------------------------
    /// Shared setting parsers
    /// ------------------------------------------------------------

    template <typename Integer> [[nodiscard]] bool parseInteger(std::string_view value, Integer &result) noexcept
    {
        const auto [end, error] = std::from_chars(value.begin(), value.end(), result);
        return error == std::errc{} && end == value.end();
    }

    [[nodiscard]] bool parseLogLevel(std::string_view value, GameWIP::Logger::Types::Level &level) noexcept
    {
        using GameWIP::Logger::Types::Level;
        if(value == "trace")
        {
            level = Level::Trace;
        }
        else if(value == "debug")
        {
            level = Level::Debug;
        }
        else if(value == "info")
        {
            level = Level::Info;
        }
        else if(value == "warn")
        {
            level = Level::Warn;
        }
        else if(value == "error")
        {
            level = Level::Error;
        }
        else if(value == "fatal")
        {
            level = Level::Fatal;
        }
        else
        {
            return false;
        }
        return true;
    }

    /// ------------------------------------------------------------
    /// Backend list parsing
    /// ------------------------------------------------------------

    [[nodiscard]] bool parseInputBackendName(std::string_view value, GameWIP::Input::Types::BackendKind &backend) noexcept
    {
        using Backend = GameWIP::Input::Types::BackendKind;
        if(value == "rawInput")
        {
            backend = Backend::Win32RawInput;
        }
        else if(value == "hid")
        {
            backend = Backend::Win32Hid;
        }
        else if(value == "xinput")
        {
            backend = Backend::XInput;
        }
        else if(value == "gameInput")
        {
            backend = Backend::GameInput;
        }
        else if(value == "directInput")
        {
            backend = Backend::DirectInput;
        }
        else if(value == "windowMessages")
        {
            backend = Backend::Win32WindowMessage;
        }
        else
        {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool parseInputBackends(std::string_view value, GameWIP::Game::Runtime::Options &options) noexcept
    {
        if(value == "none")
        {
            options.inputBackendCount = 0;
            options.inputBackendsConfigured = true;
            return true;
        }

        // Parse the full list before changing the selected backends.
        std::array<GameWIP::Input::Types::BackendKind, 6> backends{};
        std::size_t count = 0;
        std::size_t offset = 0;
        while(offset < value.size())
        {
            const std::size_t separator = value.find(',', offset);
            const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
            GameWIP::Input::Types::BackendKind backend{};
            if(end == offset || !parseInputBackendName(value.substr(offset, end - offset), backend) || count == backends.size() ||
               std::find(backends.begin(), backends.begin() + static_cast<std::ptrdiff_t>(count), backend) !=
                   backends.begin() + static_cast<std::ptrdiff_t>(count))
            {
                return false;
            }
            backends[count++] = backend;
            if(separator == std::string_view::npos)
            {
                break;
            }

            if(separator + 1 == value.size())
            {
                return false;
            }
            offset = separator + 1;
        }

        if(count == 0)
        {
            return false;
        }

        // Apply the validated backend selection.
        options.inputBackends = backends;
        options.inputBackendCount = count;
        options.inputBackendsConfigured = true;
        return true;
    }

} // namespace

namespace GameWIP::Game::Runtime
{
    Options readOptions(int argc, char *const *argv) noexcept
    {
        Options options;

        // Apply the environment default before command-line overrides.
        if(const char *rawLevel = std::getenv("GAMEWIP_LOG_LEVEL"); rawLevel != nullptr)
        {
            const std::string_view value{rawLevel};
            Logger::Types::Level level = options.minLevel;
            if(parseLogLevel(value, level))
            {
                options.minLevel = level;
                options.logLevelSource = "environment";
            }
            else
            {
                options.invalidLogLevel = value;
            }
        }

        // Parse runtime switches from left to right so the last valid value wins.
        if(argv != nullptr)
        {
            for(int index = 1; index < argc; ++index)
            {
                if(argv[index] == nullptr)
                {
                    continue;
                }

                const std::string_view argument{argv[index]};
                constexpr std::string_view kLogLevelOption = "--logLevel";
                constexpr std::string_view kLogLevelPrefix = "--logLevel=";
                constexpr std::string_view kInputDevicesOption = "--inputDevices";
                constexpr std::string_view kInputDevicesPrefix = "--inputDevices=";
                constexpr std::string_view kInputDebugOption = "--inputDebug";
                constexpr std::string_view kInputDebugPrefix = "--inputDebug=";
                constexpr std::string_view kWindowEventsOption = "--windowEvents";
                constexpr std::string_view kWindowEventsPrefix = "--windowEvents=";
                constexpr std::string_view kInputBackendsOption = "--inputBackends";
                constexpr std::string_view kInputBackendsPrefix = "--inputBackends=";

                // Select native Input backends.
                if(argument == kInputBackendsOption)
                {
                    options.inputBackendCount = 0;
                    options.inputBackendsConfigured = false;
                    options.invalidInputBackends = "<missing>";
                    continue;
                }

                if(argument.starts_with(kInputBackendsPrefix))
                {
                    const std::string_view value = argument.substr(kInputBackendsPrefix.size());
                    if(parseInputBackends(value, options))
                    {
                        options.invalidInputBackends = {};
                    }
                    else
                    {
                        options.inputBackendCount = 0;
                        options.inputBackendsConfigured = false;
                        options.invalidInputBackends = value.empty() ? "<empty>" : value;
                    }
                    continue;
                }

                // Enable diagnostic reports.
                if(argument == kInputDevicesOption)
                {
                    options.inputDevices = true;
                    options.invalidInputDevices = {};
                    continue;
                }

                if(argument.starts_with(kInputDevicesPrefix))
                {
                    const std::string_view value = argument.substr(kInputDevicesPrefix.size());
                    options.invalidInputDevices = value.empty() ? "<unexpected value>" : value;
                    continue;
                }

                if(argument == kInputDebugOption)
                {
                    options.inputDebug = true;
                    options.invalidInputDebug = {};
                    continue;
                }

                if(argument.starts_with(kInputDebugPrefix))
                {
                    const std::string_view value = argument.substr(kInputDebugPrefix.size());
                    options.invalidInputDebug = value.empty() ? "<unexpected value>" : value;
                    continue;
                }

                if(argument == kWindowEventsOption)
                {
                    options.windowEvents = true;
                    options.invalidWindowEvents = {};
                    continue;
                }

                if(argument.starts_with(kWindowEventsPrefix))
                {
                    const std::string_view value = argument.substr(kWindowEventsPrefix.size());
                    options.invalidWindowEvents = value.empty() ? "<unexpected value>" : value;
                    continue;
                }

                // Apply a command-line Logger override.
                if(argument == kLogLevelOption)
                {
                    options.invalidLogLevel = "<missing>";
                    options.invalidLogLevelSetting = kLogLevelOption;
                    continue;
                }

                if(!argument.starts_with(kLogLevelPrefix))
                {
                    continue;
                }

                const std::string_view value = argument.substr(kLogLevelPrefix.size());
                Logger::Types::Level level = options.minLevel;
                if(parseLogLevel(value, level))
                {
                    options.minLevel = level;
                    options.logLevelSource = "command_line";
                    options.invalidLogLevel = {};
                    options.invalidLogLevelSetting = {};
                }
                else
                {
                    options.invalidLogLevel = value.empty() ? std::string_view{"<empty>"} : value;
                    options.invalidLogLevelSetting = kLogLevelOption;
                }
            }
        }

        // Read the loop cadence and display target from the environment.
        if(const char *rawWait = std::getenv("GAMEWIP_EVENT_WAIT_MS"); rawWait != nullptr)
        {
            const std::string_view value{rawWait};
            std::uint32_t milliseconds = 0;
            if(parseInteger(value, milliseconds) && milliseconds >= 1 && milliseconds <= 1000)
            {
                options.eventLoopInterval = std::chrono::milliseconds{milliseconds};
            }
            else
            {
                options.invalidEventWait = value;
            }
        }

        if(const char *rawDisplay = std::getenv("GAMEWIP_DISPLAY_INDEX"); rawDisplay != nullptr)
        {
            const std::string_view value{rawDisplay};
            std::size_t index = 0;
            if(parseInteger(value, index))
            {
                options.displayIndex = index;
            }
            else
            {
                options.invalidDisplayIndex = value;
            }
        }

        return options;
    }
} // namespace GameWIP::Game::Runtime
