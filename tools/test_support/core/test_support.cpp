/// @file test_support.cpp
/// @brief Shared status formatting for the TestSupport library.

#include "test_support/types.h"

#include <format>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport
{
    std::string formatInfrastructureStatus(const Types::InfrastructureStatus &status)
    {
        std::string_view errorName = "PlatformFailure";
        switch (status.error)
        {
        case Types::InfrastructureError::None:
            errorName = "None";
            break;
        case Types::InfrastructureError::InvalidArgument:
            errorName = "InvalidArgument";
            break;
        case Types::InfrastructureError::Unsupported:
            errorName = "Unsupported";
            break;
        case Types::InfrastructureError::OutOfMemory:
            errorName = "OutOfMemory";
            break;
        case Types::InfrastructureError::EncodingFailed:
            errorName = "EncodingFailed";
            break;
        case Types::InfrastructureError::ProcessSetupFailed:
            errorName = "ProcessSetupFailed";
            break;
        case Types::InfrastructureError::ProcessLaunchFailed:
            errorName = "ProcessLaunchFailed";
            break;
        case Types::InfrastructureError::ProcessCleanupFailed:
            errorName = "ProcessCleanupFailed";
            break;
        case Types::InfrastructureError::PipeCreationFailed:
            errorName = "PipeCreationFailed";
            break;
        case Types::InfrastructureError::CaptureFailed:
            errorName = "CaptureFailed";
            break;
        case Types::InfrastructureError::WaitFailed:
            errorName = "WaitFailed";
            break;
        case Types::InfrastructureError::ProcessInspectionFailed:
            errorName = "ProcessInspectionFailed";
            break;
        case Types::InfrastructureError::EnvironmentFailed:
            errorName = "EnvironmentFailed";
            break;
        case Types::InfrastructureError::FileOperationFailed:
            errorName = "FileOperationFailed";
            break;
        case Types::InfrastructureError::PlatformFailure:
            break;
        }

        return status.nativeCode == 0 ? std::string(errorName) : std::format("{} (nativeCode={})", errorName, status.nativeCode);
    }
} // namespace GameWIP::TestSupport
