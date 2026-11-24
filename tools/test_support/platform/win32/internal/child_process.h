/// @file child_process.h
/// @brief Private Win32 child-process preparation helpers for TestSupport.

#pragma once

#include "test_support/process.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

namespace GameWIP::TestSupport::Internal::Win32
{
    /// ------------------------------------------------------------
    /// Child process preparation
    /// ------------------------------------------------------------

    struct NativeInfrastructureFailure final : std::exception
    {
        NativeInfrastructureFailure(Types::InfrastructureError failure, std::uint64_t code) noexcept
            : error(failure)
            , nativeCode(code)
        {
        }

        [[nodiscard]] const char *what() const noexcept override
        {
            return "Native process infrastructure failure";
        }

        // Failure details
        Types::InfrastructureError error;
        std::uint64_t nativeCode;
    };

    [[nodiscard]] std::wstring pathToWide(const std::filesystem::path &path);
    [[nodiscard]] std::wstring buildCommandLine(const Types::Process::Options &options);
    [[nodiscard]] std::wstring buildEnvironmentBlock(const Types::Process::Options &options);
} // namespace GameWIP::TestSupport::Internal::Win32
