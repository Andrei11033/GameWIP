/// @file test_support_test_hooks.h
/// @brief Source-tree-only deterministic failure injection for TestSupport validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include <cstdint>
#include <optional>

#ifndef TEST_SUPPORT_INTERNAL_TEST_HOOKS
#define TEST_SUPPORT_INTERNAL_TEST_HOOKS 0
#endif

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
namespace GameWIP::TestSupport::TestHooks
{
    enum class ChildProcessFailurePoint : std::uint8_t
    {
        None,
        Allocation,
        Unsupported,
        Platform,
        ProcessSetup,
        HandleSetup,
        PipeCreation,
        ProcessLaunch,
        JobAssignment,
        CaptureSetup,
        ThreadCreation,
        ThreadResume,
        CaptureRead,
        Wait,
        ProcessInspection,
        ProcessCleanup
    };

    enum class FileFailurePoint : std::uint8_t
    {
        None,
        Read,
        Write,
        Exists,
        CreateDirectories,
        Remove,
        TemporaryDirectory,
        CurrentPath
    };

    enum class EnvironmentFailurePoint : std::uint8_t
    {
        None,
        Read,
        Set,
        Unset
    };

    void reset() noexcept;
    void forceNextChildProcessFailure(ChildProcessFailurePoint point, std::uint64_t nativeCode = 1) noexcept;
    void forceNextFileFailure(FileFailurePoint point, std::uint64_t nativeCode = 1) noexcept;
    void forceNextEnvironmentFailure(EnvironmentFailurePoint point, std::uint64_t nativeCode = 1) noexcept;
} // namespace GameWIP::TestSupport::TestHooks

namespace GameWIP::TestSupport::Detail::TestHooks
{
    [[nodiscard]] std::optional<std::uint64_t> consumeChildProcessFailure(TestSupport::TestHooks::ChildProcessFailurePoint point) noexcept;
    [[nodiscard]] std::optional<std::uint64_t> consumeFileFailure(TestSupport::TestHooks::FileFailurePoint point) noexcept;
    [[nodiscard]] std::optional<std::uint64_t> consumeEnvironmentFailure(TestSupport::TestHooks::EnvironmentFailurePoint point) noexcept;
} // namespace GameWIP::TestSupport::Detail::TestHooks
#endif
