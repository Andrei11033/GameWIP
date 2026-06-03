/// @file win32_child_process.cpp
/// @brief Windows child-process backend for the GameWIP TestSupport library.

#include "test_support/test_support.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace GameWIP::TestSupport
{
#if defined(_WIN32)
    namespace
    {
        class UniqueHandle
        {
        public:
            UniqueHandle() noexcept = default;

            explicit UniqueHandle(HANDLE handle) noexcept
                : handle_(handle)
            {
            }

            ~UniqueHandle() noexcept
            {
                reset();
            }

            UniqueHandle(const UniqueHandle &) = delete;
            UniqueHandle &operator=(const UniqueHandle &) = delete;

            UniqueHandle(UniqueHandle &&other) noexcept
                : handle_(std::exchange(other.handle_, nullptr))
            {
            }

            UniqueHandle &operator=(UniqueHandle &&other) noexcept
            {
                if (this != &other)
                {
                    reset(std::exchange(other.handle_, nullptr));
                }
                return *this;
            }

            [[nodiscard]] HANDLE get() const noexcept
            {
                return handle_;
            }

            void reset(HANDLE handle = nullptr) noexcept
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }
                handle_ = handle;
            }

        private:
            HANDLE handle_ = nullptr;
        };

        [[nodiscard]] std::wstring asciiFallbackToWide(std::string_view text)
        {
            std::wstring output;
            output.reserve(text.size());
            for (char ch : text)
            {
                const unsigned char value = static_cast<unsigned char>(ch);
                output.push_back(value >= 0x20 && value < 0x80 ? static_cast<wchar_t>(value) : L'?');
            }
            return output;
        }

        [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int inputSize = static_cast<int>(text.size());
            const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
            if (wideSize <= 0)
            {
                return asciiFallbackToWide(text);
            }

            std::wstring output(static_cast<std::size_t>(wideSize), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, output.data(), wideSize) != wideSize)
            {
                return L"?";
            }
            return output;
        }

        [[nodiscard]] std::wstring pathToWide(const std::filesystem::path &path)
        {
            try
            {
                return path.wstring();
            }
            catch (...)
            {
                return utf8ToWide(path.string());
            }
        }

        [[nodiscard]] bool needsQuoting(std::wstring_view text)
        {
            return text.empty() || text.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
        }

        [[nodiscard]] std::wstring quoteWindowsArgument(std::wstring_view text)
        {
            if (!needsQuoting(text))
            {
                return std::wstring(text);
            }

            std::wstring quoted;
            quoted.push_back(L'"');

            std::size_t backslashes = 0;
            for (wchar_t ch : text)
            {
                if (ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }

                if (ch == L'"')
                {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(ch);
                    backslashes = 0;
                    continue;
                }

                quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted.push_back(ch);
            }

            quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }

        [[nodiscard]] std::wstring buildCommandLine(const Types::ChildProcessOptions &options)
        {
            std::wstring commandLine = quoteWindowsArgument(pathToWide(options.executablePath));
            for (const std::string &argument : options.arguments)
            {
                commandLine.push_back(L' ');
                commandLine += quoteWindowsArgument(utf8ToWide(argument));
            }
            return commandLine;
        }

        [[nodiscard]] std::wstring lowerEnvironmentName(std::wstring_view text)
        {
            std::wstring lowered(text);
            for (wchar_t &ch : lowered)
            {
                ch = static_cast<wchar_t>(std::towlower(ch));
            }
            return lowered;
        }

        [[nodiscard]] std::wstring environmentEntryName(std::wstring_view entry)
        {
            if (!entry.empty() && entry.front() == L'=')
            {
                const std::size_t secondEquals = entry.find(L'=', 1);
                return secondEquals == std::wstring_view::npos ? std::wstring(entry) : std::wstring(entry.substr(0, secondEquals));
            }

            const std::size_t equals = entry.find(L'=');
            return equals == std::wstring_view::npos ? std::wstring(entry) : std::wstring(entry.substr(0, equals));
        }

        [[nodiscard]] bool sameEnvironmentName(std::wstring_view left, std::wstring_view right)
        {
            return lowerEnvironmentName(left) == lowerEnvironmentName(right);
        }

        void applyEnvironmentOverride(std::vector<std::wstring> &entries, const Types::EnvironmentVariable &variable)
        {
            if (variable.name.empty() || variable.name.find('=') != std::string::npos)
            {
                return;
            }

            const std::wstring variableName = utf8ToWide(variable.name);
            const auto matchesName = [&variableName](const std::wstring &entry)
            {
                return sameEnvironmentName(environmentEntryName(entry), variableName);
            };

            if (!variable.value)
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), matchesName), entries.end());
                return;
            }

            const std::wstring replacement = variableName + L"=" + utf8ToWide(*variable.value);
            const auto existing = std::find_if(entries.begin(), entries.end(), matchesName);
            if (existing != entries.end())
            {
                *existing = replacement;
            }
            else
            {
                entries.push_back(replacement);
            }
        }

        [[nodiscard]] std::vector<std::wstring> inheritedEnvironmentEntries()
        {
            std::vector<std::wstring> entries;
            LPWCH environmentStrings = GetEnvironmentStringsW();
            if (environmentStrings == nullptr)
            {
                return entries;
            }

            for (LPWCH current = environmentStrings; *current != L'\0'; current += std::wcslen(current) + 1)
            {
                entries.emplace_back(current);
            }

            FreeEnvironmentStringsW(environmentStrings);
            return entries;
        }

        [[nodiscard]] std::wstring buildEnvironmentBlock(const Types::ChildProcessOptions &options)
        {
            std::vector<std::wstring> entries = options.inheritParentEnvironment ? inheritedEnvironmentEntries() : std::vector<std::wstring>{};

            for (const Types::EnvironmentVariable &variable : options.environment)
            {
                applyEnvironmentOverride(entries, variable);
            }

            std::sort(
                entries.begin(),
                entries.end(),
                [](const std::wstring &left, const std::wstring &right)
                {
                    return lowerEnvironmentName(environmentEntryName(left)) < lowerEnvironmentName(environmentEntryName(right));
                });

            std::wstring block;
            for (const std::wstring &entry : entries)
            {
                block += entry;
                block.push_back(L'\0');
            }
            block.push_back(L'\0');
            if (entries.empty())
            {
                block.push_back(L'\0');
            }
            return block;
        }

        [[nodiscard]] DWORD timeoutMilliseconds(std::chrono::milliseconds timeout)
        {
            if (timeout.count() < 0)
            {
                return INFINITE;
            }

            constexpr auto maxWait = static_cast<long long>((std::numeric_limits<DWORD>::max)() - 1);
            if (timeout.count() > maxWait)
            {
                return INFINITE;
            }

            return static_cast<DWORD>(timeout.count());
        }
    } // namespace
#endif

    Types::ChildProcessResult runChildProcess(const Types::ChildProcessOptions &options)
    {
        Types::ChildProcessResult result;

#if defined(_WIN32)
        std::wstring commandLine = buildCommandLine(options);
        std::wstring environmentBlock = buildEnvironmentBlock(options);

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        UniqueHandle outputRead;
        UniqueHandle outputWrite;
        if (options.captureOutput)
        {
            HANDLE outputReadRaw = nullptr;
            HANDLE outputWriteRaw = nullptr;
            if (CreatePipe(&outputReadRaw, &outputWriteRaw, &securityAttributes, 0) == FALSE)
            {
                result.exitCode = -1;
                return result;
            }

            outputRead.reset(outputReadRaw);
            outputWrite.reset(outputWriteRaw);

            if (SetHandleInformation(outputRead.get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
            {
                result.exitCode = -1;
                return result;
            }
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startupInfo.hStdOutput = options.captureOutput ? outputWrite.get() : GetStdHandle(STD_OUTPUT_HANDLE);
        startupInfo.hStdError = options.captureOutput ? outputWrite.get() : GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION processInfo{};
        const BOOL created = CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            environmentBlock.data(),
            nullptr,
            &startupInfo,
            &processInfo);

        outputWrite.reset();

        if (created == FALSE)
        {
            result.exitCode = -1;
            return result;
        }

        UniqueHandle processHandle(processInfo.hProcess);
        UniqueHandle threadHandle(processInfo.hThread);

        std::string output;
        std::thread outputReader;
        if (options.captureOutput)
        {
            try
            {
                const HANDLE outputReadHandle = outputRead.get();
                outputReader = std::thread(
                    [outputReadHandle, &output]
                    {
                        char buffer[4096];
                        DWORD bytesRead = 0;
                        while (ReadFile(outputReadHandle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) != FALSE && bytesRead > 0)
                        {
                            output.append(buffer, buffer + bytesRead);
                        }
                    });
            }
            catch (...)
            {
                TerminateProcess(processHandle.get(), 0x54455354u);
                WaitForSingleObject(processHandle.get(), INFINITE);
                result.exitCode = -1;
                result.wasTerminatedByTest = true;
                return result;
            }
        }

        const DWORD waitResult = WaitForSingleObject(processHandle.get(), timeoutMilliseconds(options.timeout));
        if (waitResult == WAIT_TIMEOUT)
        {
            result.timedOut = true;
            result.wasTerminatedByTest = true;
            TerminateProcess(processHandle.get(), 0x54455354u);
            WaitForSingleObject(processHandle.get(), INFINITE);
        }
        else if (waitResult != WAIT_OBJECT_0)
        {
            result.exitCode = -1;
            result.wasTerminatedByTest = true;
            TerminateProcess(processHandle.get(), 0x54455354u);
            WaitForSingleObject(processHandle.get(), INFINITE);
        }

        DWORD exitCode = 0;
        if (GetExitCodeProcess(processHandle.get(), &exitCode) != FALSE)
        {
            result.exitCode = static_cast<int>(exitCode);
        }
        else
        {
            result.exitCode = -1;
        }

        if (outputReader.joinable())
        {
            outputReader.join();
        }

        result.output = std::move(output);
        return result;
#else
        (void)options;
        result.exitCode = -1;
        return result;
#endif
    }
} // namespace GameWIP::TestSupport
