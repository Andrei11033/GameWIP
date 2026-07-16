/// @file win32_child_process.cpp
/// @brief Windows child-process backend for the TestSupport library.

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
#include <chrono>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace GameWIP::TestSupport
{
#if defined(_WIN32)
    namespace
    {
        /// @brief Move-only owner for Win32 process, thread, pipe, and job handles.
        class UniqueHandle
        {
        public:
            UniqueHandle() noexcept = default;

            /// @brief Takes ownership of one CloseHandle-compatible value.
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

            /// @brief Returns the owned handle without transferring it.
            [[nodiscard]] HANDLE get() const noexcept
            {
                return handle_;
            }

            /// @brief Closes current ownership and optionally takes a replacement handle.
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

        /// Owns the variable-sized attribute list that makes standard-handle inheritance an explicit allowlist.
        /// @note Unrelated inheritable parent handles must never cross the child-test boundary.
        class StartupAttributeList final
        {
        public:
            explicit StartupAttributeList(const std::vector<HANDLE> &handles)
            {
                SIZE_T requiredBytes = 0;
                static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1, 0, &requiredBytes));
                if (requiredBytes == 0)
                {
                    return;
                }

                storage_.resize(requiredBytes);
                list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
                if (InitializeProcThreadAttributeList(list_, 1, 0, &requiredBytes) == FALSE)
                {
                    list_ = nullptr;
                    return;
                }
                initialized_ = true;

                if (UpdateProcThreadAttribute(
                        list_,
                        0,
                        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                        static_cast<void *>(const_cast<HANDLE *>(handles.data())),
                        handles.size() * sizeof(HANDLE),
                        nullptr,
                        nullptr) == FALSE)
                {
                    DeleteProcThreadAttributeList(list_);
                    initialized_ = false;
                    list_ = nullptr;
                }
            }

            ~StartupAttributeList() noexcept
            {
                if (initialized_)
                {
                    DeleteProcThreadAttributeList(list_);
                }
            }

            StartupAttributeList(const StartupAttributeList &) = delete;
            StartupAttributeList &operator=(const StartupAttributeList &) = delete;

            [[nodiscard]] bool valid() const noexcept
            {
                return initialized_;
            }

            [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept
            {
                return list_;
            }

        private:
            std::vector<std::byte> storage_;
            LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
            bool initialized_ = false;
        };

        /// Duplicates an attached standard handle without changing its inheritance flags.
        /// Detached streams use an inheritable NUL handle; duplication failures are not hidden by fallback.
        [[nodiscard]] UniqueHandle inheritableStandardHandle(DWORD standardHandle, DWORD fallbackAccess)
        {
            const HANDLE source = GetStdHandle(standardHandle);
            if (source != nullptr && source != INVALID_HANDLE_VALUE)
            {
                HANDLE duplicate = nullptr;
                if (DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE)
                {
                    return UniqueHandle(duplicate);
                }
                return {};
            }

            SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = sizeof(attributes);
            attributes.bInheritHandle = TRUE;
            return UniqueHandle(CreateFileW(L"NUL", fallbackAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr));
        }

        /// Adds a valid child-side handle once; stdout and stderr may intentionally share a pipe.
        void appendInheritedHandle(std::vector<HANDLE> &handles, HANDLE handle)
        {
            if (std::find(handles.begin(), handles.end(), handle) == handles.end())
            {
                handles.push_back(handle);
            }
        }

        /// @brief Converts public UTF-8 process text to UTF-16 without lossy substitution.
        [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
        {
            if (text.find('\0') != std::string_view::npos)
            {
                throw std::invalid_argument("Child-process text contains an embedded null");
            }
            if (text.empty())
            {
                return {};
            }
            if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                throw std::length_error("Child-process text exceeds the Win32 conversion limit");
            }

            const int inputSize = static_cast<int>(text.size());
            const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
            if (wideSize <= 0)
            {
                throw std::invalid_argument("Child-process text is not valid UTF-8");
            }

            std::wstring output(static_cast<std::size_t>(wideSize), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, output.data(), wideSize) != wideSize)
            {
                throw std::runtime_error("Win32 child-process text conversion failed");
            }
            return output;
        }

        /// @brief Returns a validated native UTF-16 executable path.
        [[nodiscard]] std::wstring pathToWide(const std::filesystem::path &path)
        {
            std::wstring text = path.wstring();
            if (text.find(L'\0') != std::wstring::npos)
            {
                throw std::invalid_argument("Child-process executable path contains an embedded null");
            }
            return text;
        }

        /// @brief Returns whether CreateProcess command-line grammar requires quoting.
        [[nodiscard]] bool needsQuoting(std::wstring_view text)
        {
            return text.empty() || text.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
        }

        /// @brief Quotes one argv value using Windows backslash-before-quote rules.
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

        /// @brief Builds the mutable UTF-16 command line consumed directly by CreateProcessW.
        /// @note No shell is involved; each public argument is quoted using Windows command-line rules.
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

        /// @brief Normalizes an environment name for Windows case-insensitive comparison.
        [[nodiscard]] std::wstring lowerEnvironmentName(std::wstring_view text)
        {
            std::wstring lowered(text);
            for (wchar_t &ch : lowered)
            {
                ch = static_cast<wchar_t>(std::towlower(ch));
            }
            return lowered;
        }

        /// @brief Extracts a variable name, preserving special drive-current-directory entries.
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

        /// @brief Compares environment names using Windows case-insensitive semantics.
        [[nodiscard]] bool sameEnvironmentName(std::wstring_view left, std::wstring_view right)
        {
            return lowerEnvironmentName(left) == lowerEnvironmentName(right);
        }

        /// @brief Applies one set/unset override to inherited environment entries.
        void applyEnvironmentOverride(std::vector<std::wstring> &entries, const Types::EnvironmentVariable &variable)
        {
            if (variable.name.empty() || variable.name.find('=') != std::string::npos)
            {
                throw std::invalid_argument("Child-process environment names must be non-empty and cannot contain '='");
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

        /// @brief Copies the current process Unicode environment block into editable entries.
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

        /// @brief Builds the sorted double-null-terminated Unicode child environment block.
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

        /// @brief Converts a chrono timeout to finite or infinite Win32 wait semantics.
        [[nodiscard]] DWORD timeoutMilliseconds(std::chrono::milliseconds timeout)
        {
            if (timeout.count() < 0)
            {
                return INFINITE;
            }

            constexpr auto maxWait = static_cast<long long>((std::numeric_limits<DWORD>::max)() - 1);
            if (timeout.count() > maxWait)
            {
                return (std::numeric_limits<DWORD>::max)() - 1;
            }

            return static_cast<DWORD>(timeout.count());
        }
    } // namespace
#endif

    Types::ChildProcessResult runChildProcess(const Types::ChildProcessOptions &options)
    {
        Types::ChildProcessResult result;

#if defined(_WIN32)
        constexpr DWORD kTestTerminationCode = 0x54455354u;
        std::wstring commandLine = buildCommandLine(options);
        std::wstring environmentBlock = buildEnvironmentBlock(options);

        // The job owns the complete child tree and guarantees that closing/termination cannot leave
        // descendants alive with inherited capture handles.
        UniqueHandle jobHandle(CreateJobObjectW(nullptr, nullptr));
        if (jobHandle.get() == nullptr)
        {
            result.exitCode = 0;
            return result;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
        jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(jobHandle.get(), JobObjectExtendedLimitInformation, &jobLimits, static_cast<DWORD>(sizeof(jobLimits))) == FALSE)
        {
            result.exitCode = 0;
            return result;
        }

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
                result.exitCode = 0;
                return result;
            }

            outputRead.reset(outputReadRaw);
            outputWrite.reset(outputWriteRaw);

            if (SetHandleInformation(outputRead.get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
            {
                result.exitCode = 0;
                return result;
            }
        }

        UniqueHandle childInput = inheritableStandardHandle(STD_INPUT_HANDLE, GENERIC_READ);
        UniqueHandle childOutput;
        UniqueHandle childError;
        if (!options.captureOutput)
        {
            childOutput = inheritableStandardHandle(STD_OUTPUT_HANDLE, GENERIC_WRITE);
            childError = inheritableStandardHandle(STD_ERROR_HANDLE, GENERIC_WRITE);
        }
        if (childInput.get() == nullptr || childInput.get() == INVALID_HANDLE_VALUE ||
            (!options.captureOutput && (childOutput.get() == nullptr || childOutput.get() == INVALID_HANDLE_VALUE || childError.get() == nullptr ||
                                        childError.get() == INVALID_HANDLE_VALUE)))
        {
            result.exitCode = 0;
            return result;
        }

        // Restrict inheritance to the three selected standard handles. CREATE_SUSPENDED below then
        // gives us a chance to assign the process to the kill-on-close job before child code runs.
        std::vector<HANDLE> inheritedHandles;
        inheritedHandles.reserve(3);
        appendInheritedHandle(inheritedHandles, childInput.get());
        appendInheritedHandle(inheritedHandles, options.captureOutput ? outputWrite.get() : childOutput.get());
        appendInheritedHandle(inheritedHandles, options.captureOutput ? outputWrite.get() : childError.get());
        StartupAttributeList attributeList(inheritedHandles);
        if (!attributeList.valid())
        {
            result.exitCode = 0;
            return result;
        }

        STARTUPINFOEXW startupInfo{};
        startupInfo.StartupInfo.cb = sizeof(startupInfo);
        startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.StartupInfo.hStdInput = childInput.get();
        startupInfo.StartupInfo.hStdOutput = options.captureOutput ? outputWrite.get() : childOutput.get();
        startupInfo.StartupInfo.hStdError = options.captureOutput ? outputWrite.get() : childError.get();
        startupInfo.lpAttributeList = attributeList.get();

        PROCESS_INFORMATION processInfo{};
        const BOOL created = CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
            environmentBlock.data(),
            nullptr,
            &startupInfo.StartupInfo,
            &processInfo);

        outputWrite.reset();
        childInput.reset();
        childOutput.reset();
        childError.reset();

        if (created == FALSE)
        {
            result.exitCode = 0;
            return result;
        }

        UniqueHandle processHandle(processInfo.hProcess);
        UniqueHandle threadHandle(processInfo.hThread);

        // Assignment must succeed before ResumeThread; otherwise descendants could escape the job.
        if (AssignProcessToJobObject(jobHandle.get(), processHandle.get()) == FALSE)
        {
            TerminateProcess(processHandle.get(), kTestTerminationCode);
            WaitForSingleObject(processHandle.get(), INFINITE);
            result.exitCode = 0;
            result.wasTerminatedByTest = true;
            return result;
        }

        std::string output;
        bool outputTruncated = false;
        bool outputReadFailed = false;
        std::thread outputReader;
        UniqueHandle outputDoneEvent;
        if (options.captureOutput)
        {
            outputDoneEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (outputDoneEvent.get() == nullptr)
            {
                TerminateJobObject(jobHandle.get(), kTestTerminationCode);
                WaitForSingleObject(processHandle.get(), INFINITE);
                result.exitCode = 0;
                result.wasTerminatedByTest = true;
                return result;
            }

            try
            {
                const HANDLE outputReadHandle = outputRead.get();
                const HANDLE outputDoneHandle = outputDoneEvent.get();
                const std::size_t captureLimit = options.maxCapturedOutputBytes;
                outputReader = std::thread(
                    [outputReadHandle, outputDoneHandle, captureLimit, &output, &outputTruncated, &outputReadFailed]
                    {
                        try
                        {
                            char buffer[4096];
                            // Continue reading after the retained limit. Draining is required so a
                            // verbose child cannot block forever on a full pipe.
                            while (true)
                            {
                                DWORD bytesRead = 0;
                                if (ReadFile(outputReadHandle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) == FALSE)
                                {
                                    if (GetLastError() != ERROR_BROKEN_PIPE)
                                    {
                                        outputReadFailed = true;
                                    }
                                    break;
                                }
                                if (bytesRead == 0)
                                {
                                    break;
                                }

                                const std::size_t retained = output.size();
                                const std::size_t available = retained < captureLimit ? captureLimit - retained : 0;
                                const std::size_t appendCount = std::min<std::size_t>(available, bytesRead);
                                if (appendCount > 0)
                                {
                                    output.append(buffer, appendCount);
                                }
                                if (appendCount < bytesRead)
                                {
                                    outputTruncated = true;
                                }
                            }
                        }
                        catch (...)
                        {
                            outputReadFailed = true;
                        }
                        SetEvent(outputDoneHandle);
                    });
            }
            catch (...)
            {
                TerminateJobObject(jobHandle.get(), kTestTerminationCode);
                WaitForSingleObject(processHandle.get(), INFINITE);
                result.exitCode = 0;
                result.wasTerminatedByTest = true;
                return result;
            }
        }

        if (ResumeThread(threadHandle.get()) == static_cast<DWORD>(-1))
        {
            TerminateJobObject(jobHandle.get(), kTestTerminationCode);
            WaitForSingleObject(processHandle.get(), INFINITE);
            result.exitCode = 0;
            result.wasTerminatedByTest = true;
        }
        else
        {
            result.infrastructureFailure = false;
        }

        bool processInspectionFailed = result.infrastructureFailure;
        const DWORD waitResult = WaitForSingleObject(processHandle.get(), timeoutMilliseconds(options.timeout));
        if (waitResult == WAIT_TIMEOUT)
        {
            result.timedOut = true;
            result.wasTerminatedByTest = true;
            TerminateJobObject(jobHandle.get(), kTestTerminationCode);
            WaitForSingleObject(processHandle.get(), INFINITE);
        }
        else if (waitResult != WAIT_OBJECT_0)
        {
            processInspectionFailed = true;
            result.wasTerminatedByTest = true;
            TerminateJobObject(jobHandle.get(), kTestTerminationCode);
            WaitForSingleObject(processHandle.get(), INFINITE);
        }

        DWORD exitCode = 0;
        if (!processInspectionFailed && GetExitCodeProcess(processHandle.get(), &exitCode) != FALSE)
        {
            result.exitCode = static_cast<std::uint32_t>(exitCode);
            result.infrastructureFailure = false;
        }
        else
        {
            result.exitCode = 0;
            result.infrastructureFailure = true;
        }

        // Even after normal primary-process completion, descendants can retain stdout/stderr pipe
        // handles. Terminate the job before joining the reader so process-tree cleanup is bounded.
        TerminateJobObject(jobHandle.get(), kTestTerminationCode);

        if (outputReader.joinable())
        {
            if (WaitForSingleObject(outputDoneEvent.get(), 2000) != WAIT_OBJECT_0)
            {
                CancelSynchronousIo(reinterpret_cast<HANDLE>(outputReader.native_handle()));
                if (WaitForSingleObject(outputDoneEvent.get(), 2000) != WAIT_OBJECT_0)
                {
                    outputRead.reset();
                    WaitForSingleObject(outputDoneEvent.get(), INFINITE);
                }
            }
            outputReader.join();
        }

        if (outputReadFailed)
        {
            result.exitCode = 0;
            result.infrastructureFailure = true;
        }

        result.output = std::move(output);
        result.outputTruncated = outputTruncated;
        return result;
#else
        (void)options;
        result.exitCode = 0;
        return result;
#endif
    }
} // namespace GameWIP::TestSupport
