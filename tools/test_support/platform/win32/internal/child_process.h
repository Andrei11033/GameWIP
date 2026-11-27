/// @file child_process.h
/// @brief Private Win32 child-process preparation helpers for TestSupport.

#pragma once

#include "test_support/process.h"
#include "status/status.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

namespace GameWIP::TestSupport::Internal::Win32
{
    /// ------------------------------------------------------------
    /// Child process preparation
    /// ------------------------------------------------------------

    struct NativeInfrastructureFailure final : std::exception
    {
        NativeInfrastructureFailure(GameWIP::Status::Types::Code failure, std::uint64_t code) noexcept
            : error(failure)
            , nativeCode(
                  code == 0 ? std::nullopt
                            : std::optional<GameWIP::Status::Types::NativeCode>(GameWIP::Status::Types::NativeCode{
                                  GameWIP::Status::Types::NativeCodeDomain::Win32Error,
                                  static_cast<std::int64_t>(code)}))
        {
        }

        [[nodiscard]] const char *what() const noexcept override
        {
            return "Native process infrastructure failure";
        }

        // Failure details
        GameWIP::Status::Types::Code error;
        std::optional<GameWIP::Status::Types::NativeCode> nativeCode;
    };

    [[nodiscard]] std::wstring pathToWide(const std::filesystem::path &path);
    [[nodiscard]] std::wstring buildCommandLine(const Types::Process::Options &options);
    [[nodiscard]] std::wstring buildEnvironmentBlock(const Types::Process::Options &options);
} // namespace GameWIP::TestSupport::Internal::Win32
