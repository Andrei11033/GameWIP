/// @file child_process.cpp
/// @brief Windows child-process backend for the TestSupport library.

#include "test_support/process.h"
#include "test_support/internal/process_state.h"
#include "test_support/platform/win32/internal/child_process.h"
#include "test_support/internal/status.h"
#include "test_support/platform/win32/internal/output_capture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace GameWIP::TestSupport
{
    namespace
    {
        /// ------------------------------------------------------------
        /// Native handles and startup attributes
        /// ------------------------------------------------------------

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
                if(this != &other)
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
                if(handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
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
                // Query the attribute-list storage size.
                SIZE_T requiredBytes = 0;
                static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1, 0, &requiredBytes));

                if(requiredBytes == 0)
                {
                    nativeCode_ = GetLastError();
                    return;
                }

                // Allocate and initialize the handle allowlist.
                storage_.resize(requiredBytes);
                list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());

                if(InitializeProcThreadAttributeList(list_, 1, 0, &requiredBytes) == FALSE)
                {
                    nativeCode_ = GetLastError();
                    list_ = nullptr;
                    return;
                }

                // Limit inheritance to the explicitly selected handles.
                initialized_ = true;

                if(UpdateProcThreadAttribute(
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
                if(initialized_)
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

            if(source != nullptr && source != INVALID_HANDLE_VALUE)
            {
                HANDLE duplicate = nullptr;

                if(DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE)
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
            if(std::find(handles.begin(), handles.end(), handle) == handles.end())
            {
                handles.push_back(handle);
            }
        }

        /// ------------------------------------------------------------
        /// Timeout conversion
        /// ------------------------------------------------------------

        [[nodiscard]] DWORD timeoutMilliseconds(const std::optional<std::chrono::milliseconds> &timeout)
        {
            if(!timeout)
            {
                return INFINITE;
            }

            constexpr auto maxWait = static_cast<long long>((std::numeric_limits<DWORD>::max)() - 1);

            if(timeout->count() > maxWait)
            {
                return (std::numeric_limits<DWORD>::max)() - 1;
            }
            return static_cast<DWORD>(timeout->count());
        }

        /// ------------------------------------------------------------
        /// Process cleanup and capture-reader shutdown
        /// ------------------------------------------------------------

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

        void finishOutputReader(std::thread &outputReader, HANDLE outputDoneHandle, UniqueHandle &outputRead, Types::Status &outputStatus) noexcept
        {
            if(!outputReader.joinable())
            {
                return;
            }

            DWORD outputWaitError = ERROR_SUCCESS;
            DWORD outputWait = WaitForSingleObject(outputDoneHandle, 2000);

            if(outputWait != WAIT_OBJECT_0)
            {
                outputWaitError = outputWait == WAIT_FAILED ? GetLastError() : outputWait;

                // Cancel the synchronous read before closing the handle that supplies EOF.
                static_cast<void>(CancelSynchronousIo(reinterpret_cast<HANDLE>(outputReader.native_handle())));
                outputWait = WaitForSingleObject(outputDoneHandle, 2000);

                if(outputWait != WAIT_OBJECT_0)
                {
                    outputRead.reset();
                    static_cast<void>(WaitForSingleObject(outputDoneHandle, INFINITE));
                }
            }
            outputReader.join();

            // The reader owns outputStatus until it exits. Even a timed-out
            // wait must join before reading or publishing a competing failure.
            if(outputWaitError != ERROR_SUCCESS && outputStatus.ok())
            {
                outputStatus = Internal::failureStatus(
                    GameWIP::Status::Types::Code::ProcessCaptureFailed,
                    GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Win32Error, outputWaitError});
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Child process execution
    /// ------------------------------------------------------------

    Types::Process::Result runChildProcess(const Types::Process::Options &options) noexcept
    {
        struct ExecutionState
        {
            Types::Status status;
            Types::Process::Execution execution;
        } state;

        const auto complete = [&state]() noexcept -> Types::Process::Result
        {
            return state.status.ok() ? Types::Process::Result::success(std::move(state.execution))
                                     : Types::Process::Result::failure(std::move(state.status), std::move(state.execution));
        };

        const auto setFailure = [&state](GameWIP::Status::Types::Code code, std::uint64_t nativeCode = 0) noexcept
        {
            const auto diagnostic = nativeCode == 0 ? std::nullopt
                                                    : std::optional<GameWIP::Status::Types::NativeCode>(GameWIP::Status::Types::NativeCode{
                                                          GameWIP::Status::Types::NativeCodeDomain::Win32Error,
                                                          static_cast<std::int64_t>(nativeCode)});
            state.status = Internal::failureStatus(code, diagnostic);
        };
        const auto setFailureStatus = [&state](Types::Status status) noexcept
        {
            state.status = std::move(status);
        };
        const auto setFailureStatusIfSuccessful = [&state](Types::Status status) noexcept
        {
            if(state.status.ok())
            {
                state.status = std::move(status);
            }
        };
        const auto setFailureIfSuccessful = [&state, &setFailure](GameWIP::Status::Types::Code code, std::uint64_t nativeCode = 0) noexcept
        {
            if(state.status.ok())
            {
                setFailure(code, nativeCode);
            }
        };

        try
        {
            constexpr DWORD kTestTerminationCode = 0x54455354u;

            // Validate options before preparing native command data.
            if(options.waitTimeout && options.waitTimeout->count() < 0)
            {
                setFailure(GameWIP::Status::Types::Code::InvalidArgument);
                return complete();
            }

            if(options.workingDirectory && options.workingDirectory->empty())
            {
                setFailure(GameWIP::Status::Types::Code::InvalidArgument);
                return complete();
            }

            std::wstring commandLine = Internal::Win32::buildCommandLine(options);
            std::wstring environmentBlock;
            std::wstring workingDirectory;

            // Create a job that owns and cleans up the complete child process tree.
            UniqueHandle jobHandle(CreateJobObjectW(nullptr, nullptr));

            if(jobHandle.get() == nullptr)
            {
                setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, GetLastError());
                return complete();
            }

            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
            jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

            if(SetInformationJobObject(jobHandle.get(), JobObjectExtendedLimitInformation, &jobLimits, static_cast<DWORD>(sizeof(jobLimits))) ==
               FALSE)
            {
                setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, GetLastError());
                return complete();
            }

            // Configure child standard handles and the optional capture pipe.
            // Standard handles must be inheritable, but the parent read end must not be.
            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE;

            UniqueHandle outputRead;
            UniqueHandle outputWrite;

            if(options.captureOutput)
            {
                HANDLE outputReadRaw = nullptr;
                HANDLE outputWriteRaw = nullptr;

                if(CreatePipe(&outputReadRaw, &outputWriteRaw, &securityAttributes, 0) == FALSE)
                {
                    setFailure(GameWIP::Status::Types::Code::PipeCreationFailed, GetLastError());
                    return complete();
                }

                outputRead.reset(outputReadRaw);
                outputWrite.reset(outputWriteRaw);

                if(SetHandleInformation(outputRead.get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
                {
                    setFailure(GameWIP::Status::Types::Code::PipeCreationFailed, GetLastError());
                    return complete();
                }
            }

            UniqueHandle childInput = inheritableStandardHandle(STD_INPUT_HANDLE, GENERIC_READ);

            if(childInput.get() == nullptr || childInput.get() == INVALID_HANDLE_VALUE)
            {
                setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, GetLastError());
                return complete();
            }

            UniqueHandle childOutput;
            UniqueHandle childError;

            if(!options.captureOutput)
            {
                childOutput = inheritableStandardHandle(STD_OUTPUT_HANDLE, GENERIC_WRITE);

                if(childOutput.get() == nullptr || childOutput.get() == INVALID_HANDLE_VALUE)
                {
                    setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, GetLastError());
                    return complete();
                }

                childError = inheritableStandardHandle(STD_ERROR_HANDLE, GENERIC_WRITE);

                if(childError.get() == nullptr || childError.get() == INVALID_HANDLE_VALUE)
                {
                    setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, GetLastError());
                    return complete();
                }
            }

            // Restrict inheritance to the child handles selected above.
            // This prevents unrelated parent handles from leaking into validation children.
            std::vector<HANDLE> inheritedHandles;
            inheritedHandles.reserve(3);
            appendInheritedHandle(inheritedHandles, childInput.get());
            appendInheritedHandle(inheritedHandles, options.captureOutput ? outputWrite.get() : childOutput.get());
            appendInheritedHandle(inheritedHandles, options.captureOutput ? outputWrite.get() : childError.get());
            StartupAttributeList attributeList(inheritedHandles);

            if(!attributeList.isValid())
            {
                setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, attributeList.nativeCode());
                return complete();
            }

            STARTUPINFOEXW startupInfo{};
            startupInfo.StartupInfo.cb = sizeof(startupInfo);
            startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startupInfo.StartupInfo.hStdInput = childInput.get();
            startupInfo.StartupInfo.hStdOutput = options.captureOutput ? outputWrite.get() : childOutput.get();
            startupInfo.StartupInfo.hStdError = options.captureOutput ? outputWrite.get() : childError.get();
            startupInfo.lpAttributeList = attributeList.get();

            // Prepare the child working directory and inherited environment.
            PROCESS_INFORMATION processInfo{};
            LPCWSTR currentDirectory = nullptr;

            if(options.workingDirectory)
            {
                workingDirectory = Internal::Win32::pathToWide(*options.workingDirectory);
                currentDirectory = workingDirectory.c_str();
            }

            BOOL created = FALSE;
            {
                std::lock_guard stateLock(Internal::processStateMutex());
                environmentBlock = Internal::Win32::buildEnvironmentBlock(options);

                // Start suspended so cleanup and capture setup finish before child code runs.
                created = CreateProcessW(
                    nullptr,
                    commandLine.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                    environmentBlock.data(),
                    currentDirectory,
                    &startupInfo.StartupInfo,
                    &processInfo);
            }

            const DWORD launchError = created == FALSE ? GetLastError() : ERROR_SUCCESS;

            // Close parent copies so capture can observe EOF after the child exits.
            outputWrite.reset();
            childInput.reset();
            childOutput.reset();
            childError.reset();

            if(created == FALSE)
            {
                setFailure(GameWIP::Status::Types::Code::ProcessLaunchFailed, launchError);
                return complete();
            }

            UniqueHandle processHandle(processInfo.hProcess);
            UniqueHandle threadHandle(processInfo.hThread);

            // Attach the child to the cleanup owner.
            if(AssignProcessToJobObject(jobHandle.get(), processHandle.get()) == FALSE)
            {
                const DWORD assignmentError = GetLastError();
                terminateProcessAndWait(processHandle.get(), kTestTerminationCode);
                setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, assignmentError);
                state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                return complete();
            }

            // Start capture on a reader thread so a full pipe cannot block the wait.
            std::string outputBytes;
            bool outputTruncated = false;
            Types::Status outputStatus;
            std::thread outputReader;
            UniqueHandle outputDoneEvent;

            if(options.captureOutput)
            {
                outputDoneEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));

                if(outputDoneEvent.get() == nullptr)
                {
                    const DWORD eventError = GetLastError();
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(GameWIP::Status::Types::Code::ProcessCaptureFailed, eventError);
                    state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return complete();
                }

                try
                {
                    const HANDLE outputReadHandle = outputRead.get();
                    const HANDLE outputDoneHandle = outputDoneEvent.get();
                    const std::size_t captureLimit = options.maxCapturedOutputBytes;
                    outputReader = std::thread(
                        Internal::Win32::readCapturedOutput,
                        outputReadHandle,
                        outputDoneHandle,
                        captureLimit,
                        std::ref(outputBytes),
                        std::ref(outputTruncated),
                        std::ref(outputStatus));
                }
                catch(const std::bad_alloc &)
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(GameWIP::Status::Types::Code::OutOfMemory);
                    state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return complete();
                }
                catch(const std::system_error &error)
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailureStatus(Internal::failureStatus(GameWIP::Status::Types::Code::ProcessCaptureFailed, Internal::nativeCode(error.code())));
                    state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return complete();
                }
                catch(...)
                {
                    terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    setFailure(GameWIP::Status::Types::Code::ProcessCaptureFailed);
                    state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                    return complete();
                }
            }

            // Resume only after process setup is complete.
            const DWORD resumeResult = ResumeThread(threadHandle.get());

            if(resumeResult == static_cast<DWORD>(-1))
            {
                const DWORD resumeNativeCode = GetLastError();
                terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                setFailure(GameWIP::Status::Types::Code::ProcessSetupFailed, resumeNativeCode);
                state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
            }
            else
            {
                // Wait for completion and apply timeout cleanup.
                {
                    const DWORD waitResult = WaitForSingleObject(processHandle.get(), timeoutMilliseconds(options.waitTimeout));

                    if(waitResult == WAIT_TIMEOUT)
                    {
                        state.execution.outcome = Types::Process::Outcome::TimedOut;

                        if(TerminateJobObject(jobHandle.get(), kTestTerminationCode) == FALSE)
                        {
                            setFailure(GameWIP::Status::Types::Code::ProcessCleanupFailed, GetLastError());
                        }

                        if(WaitForSingleObject(processHandle.get(), INFINITE) == WAIT_FAILED)
                        {
                            setFailureIfSuccessful(GameWIP::Status::Types::Code::ProcessCleanupFailed, GetLastError());
                        }
                    }
                    else if(waitResult != WAIT_OBJECT_0)
                    {
                        const DWORD waitError = GetLastError();
                        setFailure(GameWIP::Status::Types::Code::ProcessWaitFailed, waitError);
                        state.execution.outcome = Types::Process::Outcome::TerminatedDuringCleanup;
                        terminateJobAndWait(jobHandle.get(), processHandle.get(), kTestTerminationCode);
                    }
                    else
                    {
                        DWORD exitCode = 0;

                        if(GetExitCodeProcess(processHandle.get(), &exitCode) != FALSE)
                        {
                            state.execution.exitCode = static_cast<std::uint32_t>(exitCode);
                            state.execution.outcome = Types::Process::Outcome::Exited;
                        }
                        else
                        {
                            setFailure(GameWIP::Status::Types::Code::ProcessInspectionFailed, GetLastError());
                            state.execution.outcome = Types::Process::Outcome::OutcomeUnavailable;
                        }

                        if(TerminateJobObject(jobHandle.get(), kTestTerminationCode) == FALSE)
                        {
                            setFailureIfSuccessful(GameWIP::Status::Types::Code::ProcessCleanupFailed, GetLastError());
                        }
                    }
                }
            }

            // Join capture and publish the retained child output.
            if(outputReader.joinable())
            {
                finishOutputReader(outputReader, outputDoneEvent.get(), outputRead, outputStatus);
            }

            if(!outputStatus.ok())
            {
                setFailureStatusIfSuccessful(outputStatus);
            }

            state.execution.outputTruncated = outputTruncated;
            state.execution.outputBytes = std::move(outputBytes);
            return complete();
        }
        catch(const Internal::Win32::NativeInfrastructureFailure &failure)
        {
            setFailureStatus(Internal::failureStatus(failure.error, failure.nativeCode));
            return complete();
        }
        catch(const std::bad_alloc &)
        {
            setFailure(GameWIP::Status::Types::Code::OutOfMemory);
            return complete();
        }
        catch(const std::invalid_argument &)
        {
            setFailure(GameWIP::Status::Types::Code::InvalidArgument);
            return complete();
        }
        catch(const std::length_error &)
        {
            setFailure(GameWIP::Status::Types::Code::InvalidArgument);
            return complete();
        }
        catch(const std::system_error &error)
        {
            setFailureStatus(Internal::failureStatus(GameWIP::Status::Types::Code::NativeFailure, Internal::nativeCode(error.code())));
            return complete();
        }
        catch(...)
        {
            setFailure(GameWIP::Status::Types::Code::NativeFailure);
            return complete();
        }
    }
} // namespace GameWIP::TestSupport
