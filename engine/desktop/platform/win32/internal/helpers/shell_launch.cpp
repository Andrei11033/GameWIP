/// @file shell_launch.cpp
/// @brief Win32 shell launch-command construction.

#include "desktop/platform/win32/internal/win32_shell_launch.h"

#include "desktop/platform/win32/internal/win32_unicode.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace GameWIP::Desktop::Platform
{
    using IO::Types::ErrorCode;

    namespace
    {
        /// @brief Appends one Windows command-line argument using native quoting rules.
        void appendQuotedArgument(std::wstring_view argument, std::wstring &commandLine)
        {
            commandLine.push_back(L'"');
            std::size_t backslashes = 0;

            for(wchar_t character : argument)
            {
                if(character == L'\\')
                {
                    ++backslashes;
                    continue;
                }

                if(character == L'"')
                {
                    commandLine.append(backslashes * 2 + 1, L'\\');
                    commandLine.push_back(L'"');
                    backslashes = 0;
                    continue;
                }
                commandLine.append(backslashes, L'\\');
                backslashes = 0;
                commandLine.push_back(character);
            }
            commandLine.append(backslashes * 2, L'\\');
            commandLine.push_back(L'"');
        }

        [[nodiscard]] IO::Types::Status launchArgumentText(
            const Types::Shell::LaunchArgumentView &argument,
            std::wstring &text,
            bool allowPlaceholders)
        {
            DWORD nativeCode = ERROR_SUCCESS;

            if(const auto *literal = std::get_if<Types::Shell::LiteralLaunchArgumentView>(&argument))
            {
                if(!utf8ToUtf16(literal->text, text, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }
                return IO::successStatus();
            }

            if(const auto *target = std::get_if<Types::Shell::TargetLaunchArgumentView>(&argument))
            {
                if(const auto *path = std::get_if<Types::Shell::PathTargetView>(&target->target))
                {
                    text = path->path.get().wstring();
                    return IO::successStatus();
                }

                if(const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target->target))
                {
                    if(!utf8ToUtf16(uri->uri, text, nativeCode))
                    {
                        return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    }
                    return IO::successStatus();
                }
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(!allowPlaceholders)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            switch(std::get<Types::Shell::LaunchPlaceholder>(argument))
            {
            case Types::Shell::LaunchPlaceholder::SelectedPath:
            case Types::Shell::LaunchPlaceholder::SelectedUri:
                text = L"%1";
                return IO::successStatus();
            case Types::Shell::LaunchPlaceholder::AllPaths:
            case Types::Shell::LaunchPlaceholder::AllUris:
                text = L"%*";
                return IO::successStatus();
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
        }

    } // namespace

    [[nodiscard]] IO::Types::Status makeLaunchCommand(
        const Types::Shell::LaunchActionView &action,
        bool allowPlaceholders,
        std::wstring &executable,
        std::wstring &arguments)
    {
        // Shell association parsing rejects generic separators accepted by CreateProcess.
        // Convert a copy at the native boundary; retain the caller's portable spelling.
        auto nativeExecutable = action.executable.path.get();
        executable = nativeExecutable.make_preferred().wstring();

        if(executable.empty())
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        // Convert and quote each argument in the caller's declared order.
        arguments.clear();

        for(const Types::Shell::LaunchArgumentView &argument : action.arguments)
        {
            std::wstring text;
            IO::Types::Status status = launchArgumentText(argument, text, allowPlaceholders);

            if(!status.ok())
            {
                return status;
            }

            if(!arguments.empty())
            {
                arguments.push_back(L' ');
            }

            if(text == L"%*")
            {
                arguments += text;
            }
            else
            {
                appendQuotedArgument(text, arguments);
            }
        }

        return IO::successStatus();
    }

} // namespace GameWIP::Desktop::Platform
