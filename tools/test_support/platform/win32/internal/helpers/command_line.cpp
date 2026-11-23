/// @file command_line.cpp
/// @brief Win32 command-line construction for TestSupport child processes.

#include "test_support/platform/win32/internal/child_process.h"
#include "test_support/platform/win32/internal/text.h"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Internal::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Path conversion
    /// ------------------------------------------------------------

    std::wstring pathToWide(const std::filesystem::path &path)
    {
        std::wstring text = path.wstring();

        if(text.contains(L'\0'))
        {
            throw std::invalid_argument("Child-process path contains an embedded null");
        }
        return text;
    }

    namespace
    {
        /// ------------------------------------------------------------
        /// Command-line argument quoting
        /// ------------------------------------------------------------

        [[nodiscard]] bool needsQuoting(std::wstring_view text)
        {
            return text.empty() || text.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
        }

        [[nodiscard]] std::wstring quoteWindowsArgument(std::wstring_view text)
        {
            if(!needsQuoting(text))
            {
                return std::wstring(text);
            }

            std::wstring quoted;
            quoted.push_back(L'"');
            std::size_t backslashes = 0;

            for(wchar_t ch : text)
            {
                if(ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }

                if(ch == L'"')
                {
                    // Double backslashes before quotes so Windows parses the argument unchanged.
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(ch);
                    backslashes = 0;
                    continue;
                }
                quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted.push_back(ch);
            }
            // Double trailing backslashes before the closing quote.
            quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }

    } // namespace
    /// ------------------------------------------------------------
    /// Command-line construction
    /// ------------------------------------------------------------

    std::wstring buildCommandLine(const Types::Process::Options &options)
    {
        std::wstring commandLine = quoteWindowsArgument(pathToWide(options.executablePath));

        for(const std::string &argument : options.arguments)
        {
            commandLine.push_back(L' ');
            commandLine += quoteWindowsArgument(Internal::Platform::Win32::utf8ToWide(argument));
        }

        return commandLine;
    }
} // namespace GameWIP::TestSupport::Internal::Platform::Win32
