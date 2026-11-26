/// @file output_capture.cpp
/// @brief Win32 child-process pipe draining for TestSupport.

#include "test_support/platform/win32/internal/output_capture.h"
#include "test_support/internal/status.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>

namespace GameWIP::TestSupport::Internal::Win32
{
    /// ------------------------------------------------------------
    /// Captured child output
    /// ------------------------------------------------------------

    void readCapturedOutput(
        HANDLE outputReadHandle,
        HANDLE outputDoneHandle,
        std::size_t captureLimit,
        std::string &outputBytes,
        bool &outputTruncated,
        Types::Status &outputStatus) noexcept
    {
        const auto setOutputFailure = [&outputStatus](GameWIP::Status::Types::Code code, std::uint64_t nativeCode = 0) noexcept
        {
            if(outputStatus.ok())
            {
                const auto native = nativeCode == 0
                                        ? std::nullopt
                                        : std::optional<GameWIP::Status::Types::NativeCode>(
                                              GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Win32Error,
                                                                               static_cast<std::int64_t>(nativeCode)});
                outputStatus = Internal::failureStatus(code, native);
            }
        };

        try
        {
            char buffer[4096];

            // Drain all output while retaining only the configured prefix.
            while(true)
            {
                DWORD bytesRead = 0;

                if(ReadFile(outputReadHandle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) == FALSE)
                {
                    const DWORD readError = GetLastError();

                    if(readError != ERROR_BROKEN_PIPE)
                    {
                        setOutputFailure(GameWIP::Status::Types::Code::ProcessCaptureFailed, readError);
                    }
                    break;
                }

                if(bytesRead == 0)
                {
                    break;
                }

                const std::size_t retained = outputBytes.size();
                const std::size_t available = retained < captureLimit ? captureLimit - retained : 0;
                const std::size_t appendCount = std::min<std::size_t>(available, bytesRead);

                if(appendCount > 0)
                {
                    outputBytes.append(buffer, appendCount);
                }

                if(appendCount < bytesRead)
                {
                    outputTruncated = true;
                }
            }
        }
        catch(const std::bad_alloc &)
        {
            setOutputFailure(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            setOutputFailure(GameWIP::Status::Types::Code::ProcessCaptureFailed);
        }

        // Release the process thread waiting for the reader.
        if(SetEvent(outputDoneHandle) == FALSE)
        {
            setOutputFailure(GameWIP::Status::Types::Code::ProcessCaptureFailed, GetLastError());
        }
    }
} // namespace GameWIP::TestSupport::Internal::Win32
