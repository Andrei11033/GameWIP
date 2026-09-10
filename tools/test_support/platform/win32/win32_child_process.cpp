/// @file win32_child_process.cpp
/// @brief Windows child-process backend for the TestSupport library.

#include "test_support/process.h"
#include "test_support/internal/win32_text.h"
#include "test_support/internal/test_support_test_hooks.h"

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
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace GameWIP::TestSupport
{
#if defined(_WIN32)
    namespace
    {
        // ------------------------------------------------------------
        // Native handles and startup attributes
        // ------------------------------------------------------------

        struct NativeInfrastructureFailure
        {
            Types::InfrastructureError error;
            std::uint64_t nativeCode;
        };

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

        class StartupAttributeList final
        {
        public:
            explicit StartupAttributeList(const std::vector<HANDLE> &handles)
            {
                SIZE_T requiredBytes = 0;
                static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1, 0, &requiredBytes));
                if (requiredBytes == 0)
                {
                    nativeCode_ = GetLastError();
                    return;
                }

                storage_.resize(requiredBytes);
                list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
                if (InitializeProcThreadAttributeList(list_, 1, 0, &requiredBytes) == FALSE)
                {
                    nativeCode_ = GetLastError();
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
                    nativeCode_ = GetLastError();
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

            [[nodiscard]] bool isValid() const noexcept
            {
                return initialized_;
            }

            [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept
            {
                return list_;
            }

            [[nodiscard]] DWORD nativeCode() const noexcept
            {
                return nativeCode_;
            }

        private:
            std::vector<std::byte> storage_;
            LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
            bool initialized_ = false;
            DWORD nativeCode_ = ERROR_SUCCESS;
        };

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

        void appendInheritedHandle(std::vector<HANDLE> &handles, HANDLE handle)
        {
            if (std::find(handles.begin(), handles.end(), handle) == handles.end())
            {
                handles.push_back(handle);
            }
        }

        // ------------------------------------------------------------
        // UTF-8, path, and command-line conversion
        // ------------------------------------------------------------

        [[nodiscard]] std::wstring pathToWide(const std::filesystem::path &path)
        {
            std::wstring text = path.wstring();
            if (text.find(L'\0') != std::wstring::npos)
            {
                throw std::invalid_argument("Child-process executable path contains an embedded null");
            }
            return text;
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

        [[nodiscard]] std::wstring buildCommandLine(const Types::Process::Options &options)
        {
            std::wstring commandLine = quoteWindowsArgument(pathToWide(options.executablePath));
            for (const std::string &argument : options.arguments)
            {
                commandLine.push_back(L' ');
                commandLine += quoteWindowsArgument(Detail::Win32::utf8ToWide(argument));
            }
            return commandLine;
        }

        // ------------------------------------------------------------
        // Child environment block construction
        // ------------------------------------------------------------

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

        void applyEnvironmentOverride(std::vector<std::wstring> &entries, const Types::Process::EnvironmentOverride &variable)
        {
            if (variable.name.empty() || variable.name.find('=') != std::string::npos)
            {
                throw std::invalid_argument("Child-process environment names must be non-empty and cannot contain '='");
            }

            const std::wstring variableName = Detail::Win32::utf8ToWide(variable.name);
            const auto matchesName = [&variableName](const std::wstring &entry)
            {
                return sameEnvironmentName(environmentEntryName(entry), variableName);
            };

            if (!variable.value)
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), matchesName), entries.end());
                return;
            }

            const std::wstring replacement = variableName + L"=" + Detail::Win32::utf8ToWide(*variable.value);
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
                throw NativeInfrastructureFailure{.error = Types::InfrastructureError::ProcessSetupFailed, .nativeCode = GetLastError()};
            }

            try
            {
                // The returned environment block is documented as double-NUL-terminated and has no separate length API.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
                for (LPWCH current = environmentStrings; *current != L'\0'; current += std::wcslen(current) + 1)
                {
                    entries.emplace_back(current);
                }
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
            }
            catch (...)
            {
                static_cast<void>(FreeEnvironmentStringsW(environmentStrings));
                throw;
            }

            if (FreeEnvironmentStringsW(environmentStrings) == FALSE)
            {
                throw NativeInfrastructureFailure{.error = Types::InfrastructureError::ProcessSetupFailed, .nativeCode = GetLastError()};
            }
            return entries;
        }

        [[nodiscard]] std::wstring buildEnvironmentBlock(const Types::Process::Options &options)
        {
            std::vector<std::wstring> entries = options.inheritParentEnvironment ? inheritedEnvironmentEntries() : std::vector<std::wstring>{};
            for (const Types::Process::EnvironmentOverride &variable : options.environmentOverrides)
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

        // ------------------------------------------------------------
        // Timeout conversion
        // ------------------------------------------------------------

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

        // ------------------------------------------------------------
        // Process cleanup and output capture
        // ------------------------------------------------------------

        void terminateProcessAndWait(HANDLE processHandle, DWORD terminationCode) noexcept
        {
            static_cast<void>(TerminateProcess(processHandle, terminationCode));
            static_cast<void>(WaitForSingleObject(processHandle, INFINITE));
        }

        void terminateJobAndWait(HANDLE jobHandle, HANDLE processHandle, DWORD terminationCode) noexcept
        {
            static_cast<void>(TerminateJobObject(jobHandle, terminationCode));
            static_cast<void>(WaitForSingleObject(processHandle, INFINITE));
        }

        void readCapturedOutput(
            HANDLE outputReadHandle,
            HANDLE outputDoneHandle,
            std::size_t captureLimit,
            std::string &outputBytes,
            bool &outputTruncated,
            Types::InfrastructureStatus &outputStatus) noexcept
        {
            const auto setOutputFailure = [&outputStatus](Types::InfrastructureError error, std::uint64_t nativeCode = 0) noexcept
            {
                if (outputStatus.ok())
                {
                    outputStatus.error = error;
                    outputStatus.nativeCode = nativeCode;
                }
            };

            try
            {
                char buffer[4096];
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::CaptureRead))
                {
                    setOutputFailure(Types::InfrastructureError::CaptureFailed, *injected);
                }
                else
#endif
                {
                    while (true)
                    {
                        DWORD bytesRead = 0;
                        if (ReadFile(outputReadHandle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) == FALSE)
                        {
                            const DWORD readError = GetLastError();
                            if (readError != ERROR_BROKEN_PIPE)
                            {
                                setOutputFailure(Types::InfrastructureError::CaptureFailed, readError);
                            }
                            break;
                        }
                        if (bytesRead == 0)
                        {
                            break;
                        }

                        const std::size_t retained = outputBytes.size();
                        const std::size_t available = retained < captureLimit ? captureLimit - retained : 0;
                        const std::size_t appendCount = std::min<std::size_t>(available, bytesRead);
                        if (appendCount > 0)
                        {
                            outputBytes.append(buffer, appendCount);
                        }
                        if (appendCount < bytesRead)
                        {
                            outputTruncated = true;
                        }
                    }
                }
            }
            catch (const std::bad_alloc &)
            {
                setOutputFailure(Types::InfrastructureError::OutOfMemory);
            }
            catch (...)
            {
                setOutputFailure(Types::InfrastructureError::CaptureFailed);
            }

            if (SetEvent(outputDoneHandle) == FALSE)
            {
                setOutputFailure(Types::InfrastructureError::CaptureFailed, GetLastError());
            }
        }

        void finishOutputReader(
            std::thread &outputReader,
            HANDLE outputDoneHandle,
            UniqueHandle &outputRead,
            Types::InfrastructureStatus &outputStatus) noexcept
        {
            if (!outputReader.joinable())
            {
                return;
            }

            DWORD outputWaitError = ERROR_SUCCESS;
            DWORD outputWait = WaitForSingleObject(outputDoneHandle, 2000);
            if (outputWait != WAIT_OBJECT_0)
            {
                outputWaitError = outputWait == WAIT_FAILED ? GetLastError() : outputWait;

                // Cancel the synchronous read before closing the handle that supplies EOF.
                static_cast<void>(CancelSynchronousIo(reinterpret_cast<HANDLE>(outputReader.native_handle())));
                outputWait = WaitForSingleObject(outputDoneHandle, 2000);
                if (outputWait != WAIT_OBJECT_0)
                {
                    outputRead.reset();
                    static_cast<void>(WaitForSingleObject(outputDoneHandle, INFINITE));
                }
            }
            outputReader.join();

            // The reader owns outputStatus until it exits. Even a timed-out
            // wait must join before reading or publishing a competing failure.
            if (outputWaitError != ERROR_SUCCESS && outputStatus.ok())
            {
                outputStatus.error = Types::InfrastructureError::CaptureFailed;
                outputStatus.nativeCode = outputWaitError;
            }
        }
    } // namespace
#endif

    // ------------------------------------------------------------
    // Child process execution
    // ------------------------------------------------------------

    Types::Process::Result runChildProcess(const Types::Process::Options &options) noexcept
    {
        Types::Process::Result result;

#if defined(_WIN32)
        const auto setFailure = [&result](Types::InfrastructureError error, std::uint64_t nativeCode = 0) noexcept
        {
            result.status.error = error;
            result.status.nativeCode = nativeCode;
        };
        const auto setFailureIfSuccessful = [&result](Types::InfrastructureError error, std::uint64_t nativeCode = 0) noexcept
        {
            if (result.status.ok())
            {
                result.status.error = error;
                result.status.nativeCode = nativeCode;
            }
        };

        try
        {
            constexpr DWORD kTestTerminationCode = 0x54455354u;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::Allocation))
            {
                setFailure(Types::InfrastructureError::OutOfMemory, *injected);
                return result;
            }
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::Unsupported))
            {
                setFailure(Types::InfrastructureError::Unsupported, *injected);
                return result;
            }
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::Platform))
            {
                setFailure(Types::InfrastructureError::PlatformFailure, *injected);
                return result;
            }
#endif
            std::wstring commandLine = buildCommandLine(options);
            std::wstring environmentBlock = buildEnvironmentBlock(options);

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ProcessSetup))
            {
                setFailure(Types::InfrastructureError::ProcessSetupFailed, *injected);
                return result;
            }
#endif
            // A job object gives timeout and setup-failure cleanup one owner for the
            // whole child process tree, not just the first process handle.
            UniqueHandle jobHandle(CreateJobObjectW(nullptr, nullptr));
            if (jobHandle.get() == nullptr)
            {
                setFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
                return result;
            }

            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
            jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (SetInformationJobObject(jobHandle.get(), JobObjectExtendedLimitInformation, &jobLimits, static_cast<DWORD>(sizeof(jobLimits))) ==
                FALSE)
            {
                setFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
                return result;
            }

            // Standard handles must be inheritable for CreateProcessW, but the parent
            // read end of the capture pipe must be made non-inheritable before launch.
            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE;

            UniqueHandle outputRead;
            UniqueHandle outputWrite;
            if (options.captureOutput)
            {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::PipeCreation))
                {
                    setFailure(Types::InfrastructureError::PipeCreationFailed, *injected);
                    return result;
                }
#endif
                HANDLE outputReadRaw = nullptr;
                HANDLE outputWriteRaw = nullptr;
                if (CreatePipe(&outputReadRaw, &outputWriteRaw, &securityAttributes, 0) == FALSE)
                {
                    setFailure(Types::InfrastructureError::PipeCreationFailed, GetLastError());
                    return result;
                }
                outputRead.reset(outputReadRaw);
                outputWrite.reset(outputWriteRaw);
                if (SetHandleInformation(outputRead.get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
                {
                    setFailure(Types::InfrastructureError::PipeCreationFailed, GetLastError());
                    return result;
                }
            }

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::HandleSetup))
            {
                setFailure(Types::InfrastructureError::ProcessSetupFailed, *injected);
                return result;
            }
#endif
            UniqueHandle childInput = inheritableStandardHandle(STD_INPUT_HANDLE, GENERIC_READ);
            if (childInput.get() == nullptr || childInput.get() == INVALID_HANDLE_VALUE)
            {
                setFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
                return result;
            }

            UniqueHandle childOutput;
            UniqueHandle childError;
            if (!options.captureOutput)
            {
                childOutput = inheritableStandardHandle(STD_OUTPUT_HANDLE, GENERIC_WRITE);
                if (childOutput.get() == nullptr || childOutput.get() == INVALID_HANDLE_VALUE)
                {
                    setFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
                    return result;
                }
                childError = inheritableStandardHandle(STD_ERROR_HANDLE, GENERIC_WRITE);
                if (childError.get() == nullptr || childError.get() == INVALID_HANDLE_VALUE)
                {
                    setFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
                    return result;
                }
            }

            // PROC_THREAD_ATTRIBUTE_HANDLE_LIST prevents unrelated inheritable handles
            // in the parent process from leaking into validation children.
            std::vector<HANDLE> inheritedHandles;
            inheritedHandles.reserve(3);
            appendInheritedHandle(inheritedHandles, childInput.get());
            appendInheritedHandle(inheritedHandles, options.captureOutput ? outputWrite.get() : childOutput.get());
            appendInheritedHandle(inheritedHandles, options.captureOutput ? outputWrite.get() : childError.get());
            StartupAttributeList attributeList(inheritedHandles);
            if (!attributeList.isValid())
            {
                setFailure(Types::InfrastructureError::ProcessSetupFailed, attributeList.nativeCode());
                return result;
            }

            STARTUPINFOEXW startupInfo{};
            startupInfo.StartupInfo.cb = sizeof(startupInfo);
            startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startupInfo.StartupInfo.hStdInput = childInput.get();
            startupInfo.StartupInfo.hStdOutput = options.captureOutput ? outputWrite.get() : childOutput.get();
            startupInfo.StartupInfo.hStdError = options.captureOutput ? outputWrite.get() : childError.get();
            startupInfo.lpAttributeList = attributeList.get();

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ProcessLaunch))
            {
                setFailure(Types::InfrastructureError::ProcessLaunchFailed, *injected);
                return result;
            }
#endif
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
            const DWORD launchError = created == FALSE ? GetLastError() : ERROR_SUCCESS;

            // Close parent-owned duplicate write handles immediately so the capture
            // reader can observe EOF after the child closes its inherited handles.
            outputWrite.reset();
            childInput.reset();
            childOutput.reset();
            childError.reset();

            if (created == FALSE)
            {
                setFailure(Types::InfrastructureError::ProcessLaunchFailed, launchError);
                return result;
            }

            UniqueHandle processHandle(processInfo.hProcess);
            UniqueHandle threadHandle(processInfo.hThread);

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::JobAssignment))
            {
                terminateProcessAndWait(processHandle.get(), kTestTerminationCode);
                setFailure(Types::InfrastructureError::ProcessSetupFailed, *injected);
                result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                return result;
            }
#endif
            if (AssignProcessToJobObject(jobHandle.get(), processHandle.get()) == FALSE)
            {
                const DWORD assignmentError = GetLastError();
                terminateProcessAndWait(processHandle.get(), kTestTerminationCode);
                setFailure(Types::InfrastructureError::ProcessSetupFailed, assignmentError);
                result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                return result;
            }

            // Output capture runs on a separate reader so a verbose child cannot block
            // forever on a full pipe while the parent waits for process exit.
            std::string outputBytes;
            bool outputTruncated = false;
            Types::InfrastructureStatus outputStatus;
            std::thread outputReader;
            UniqueHandle outputDoneEvent;
            if (options.captureOutput)
            {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::CaptureSetup))
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(Types::InfrastructureError::CaptureFailed, *injected);
                    result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return result;
                }
#endif
                outputDoneEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (outputDoneEvent.get() == nullptr)
                {
                    const DWORD eventError = GetLastError();
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(Types::InfrastructureError::CaptureFailed, eventError);
                    result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return result;
                }

                try
                {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                    if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ThreadCreation))
                    {
                        terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                        setFailure(Types::InfrastructureError::CaptureFailed, *injected);
                        result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                        return result;
                    }
#endif
                    const HANDLE outputReadHandle = outputRead.get();
                    const HANDLE outputDoneHandle = outputDoneEvent.get();
                    const std::size_t captureLimit = options.maxCapturedOutputBytes;
                    outputReader = std::thread(
                        readCapturedOutput,
                        outputReadHandle,
                        outputDoneHandle,
                        captureLimit,
                        std::ref(outputBytes),
                        std::ref(outputTruncated),
                        std::ref(outputStatus));
                }
                catch (const std::bad_alloc &)
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(Types::InfrastructureError::OutOfMemory);
                    result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return result;
                }
                catch (const std::system_error &error)
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(
                        Types::InfrastructureError::CaptureFailed,
                        static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.code().value())));
                    result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return result;
                }
                catch (...)
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(Types::InfrastructureError::CaptureFailed);
                    result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return result;
                }
            }

            // The child starts suspended so setup can attach the job object and capture
            // reader before any child code runs.
            bool resumeFailed = false;
            std::uint64_t resumeNativeCode = 0;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
            if (const auto injected = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ThreadResume))
            {
                resumeFailed = true;
                resumeNativeCode = *injected;
            }
#endif
            if (!resumeFailed && ResumeThread(threadHandle.get()) == static_cast<DWORD>(-1))
            {
                resumeFailed = true;
                resumeNativeCode = GetLastError();
            }

            if (resumeFailed)
            {
                terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                setFailure(Types::InfrastructureError::ProcessSetupFailed, resumeNativeCode);
                result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
            }
            else
            {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                if (const auto waitFailure = Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::Wait))
                {
                    setFailure(Types::InfrastructureError::WaitFailed, *waitFailure);
                    result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                }
                else
#endif
                {
                    const DWORD waitResult = WaitForSingleObject(processHandle.get(), timeoutMilliseconds(options.timeout));
                    if (waitResult == WAIT_TIMEOUT)
                    {
                        result.outcome = Types::Process::Outcome::TimedOut;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                        if (const auto cleanupFailure =
                                Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ProcessCleanup))
                        {
                            setFailure(Types::InfrastructureError::ProcessCleanupFailed, *cleanupFailure);
                        }
#endif
                        if (TerminateJobObject(jobHandle.get(), kTestTerminationCode) == FALSE)
                        {
                            setFailure(Types::InfrastructureError::ProcessCleanupFailed, GetLastError());
                        }
                        if (WaitForSingleObject(processHandle.get(), INFINITE) == WAIT_FAILED)
                        {
                            setFailureIfSuccessful(Types::InfrastructureError::ProcessCleanupFailed, GetLastError());
                        }
                    }
                    else if (waitResult != WAIT_OBJECT_0)
                    {
                        const DWORD waitError = GetLastError();
                        setFailure(Types::InfrastructureError::WaitFailed, waitError);
                        result.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                        terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    }
                    else
                    {
                        DWORD exitCode = 0;
                        bool inspectionFailed = false;
                        std::uint64_t inspectionNativeCode = 0;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                        if (const auto inspectionFailure =
                                Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ProcessInspection))
                        {
                            inspectionFailed = true;
                            inspectionNativeCode = *inspectionFailure;
                        }
#endif
                        if (!inspectionFailed && GetExitCodeProcess(processHandle.get(), &exitCode) == FALSE)
                        {
                            inspectionFailed = true;
                            inspectionNativeCode = GetLastError();
                        }

                        if (!inspectionFailed)
                        {
                            result.exitCode = static_cast<std::uint32_t>(exitCode);
                            result.outcome = Types::Process::Outcome::Exited;
                        }
                        else
                        {
                            setFailure(Types::InfrastructureError::ProcessInspectionFailed, inspectionNativeCode);
                            result.outcome = Types::Process::Outcome::OutcomeUnavailable;
                        }

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
                        if (const auto cleanupFailure =
                                Detail::TestHooks::consumeChildProcessFailure(TestHooks::ChildProcessFailurePoint::ProcessCleanup))
                        {
                            setFailureIfSuccessful(Types::InfrastructureError::ProcessCleanupFailed, *cleanupFailure);
                        }
#endif
                        if (TerminateJobObject(jobHandle.get(), kTestTerminationCode) == FALSE)
                        {
                            setFailureIfSuccessful(Types::InfrastructureError::ProcessCleanupFailed, GetLastError());
                        }
                    }
                }
            }

            if (outputReader.joinable())
            {
                finishOutputReader(outputReader, outputDoneEvent.get(), outputRead, outputStatus);
            }

            if (!outputStatus.ok())
            {
                setFailureIfSuccessful(outputStatus.error, outputStatus.nativeCode);
            }

            result.outputTruncated = outputTruncated;
            result.outputBytes = std::move(outputBytes);
            return result;
        }
        catch (const NativeInfrastructureFailure &failure)
        {
            setFailure(failure.error, failure.nativeCode);
            return result;
        }
        catch (const std::bad_alloc &)
        {
            setFailure(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (const std::invalid_argument &)
        {
            setFailure(Types::InfrastructureError::InvalidArgument);
            return result;
        }
        catch (const std::length_error &)
        {
            setFailure(Types::InfrastructureError::InvalidArgument);
            return result;
        }
        catch (const std::system_error &error)
        {
            setFailure(Types::InfrastructureError::PlatformFailure, static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.code().value())));
            return result;
        }
        catch (...)
        {
            setFailure(Types::InfrastructureError::PlatformFailure);
            return result;
        }
#else
        (void)options;
        result.status.error = Types::InfrastructureError::Unsupported;
        return result;
#endif
    }
} // namespace GameWIP::TestSupport
