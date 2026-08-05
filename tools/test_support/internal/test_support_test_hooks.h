/// @file test_support_test_hooks.h
/// @brief Source-tree-only deterministic failure injection for TestSupport validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "test_support/test_support.h"

#include <cstdint>
#include <optional>

#ifndef INTERNAL_TEST_SUPPORT_TEST_HOOKS
#define INTERNAL_TEST_SUPPORT_TEST_HOOKS 0
#endif

#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
namespace GameWIP::TestSupport::TestHooks
{
    /// @brief One-shot child-process infrastructure failure points.
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

    /// @brief One-shot file and filesystem guard failure points.
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

    /// @brief One-shot environment backend failure points.
    enum class EnvironmentFailurePoint : std::uint8_t
    {
        None,
        Read,
        Set,
        Unset
    };

    /// @brief Clears every pending TestSupport failure injection.
    void reset() noexcept;

    /// @brief Forces the next matching child-process operation to fail once.
    /// @param point Failure point consumed by the next matching operation.
    /// @param nativeCode Synthetic native diagnostic returned with the failure.
    void forceNextChildProcessFailure(ChildProcessFailurePoint point, std::uint64_t nativeCode = 1) noexcept;

    /// @brief Forces the next matching file or filesystem-guard operation to fail once.
    void forceNextFileFailure(FileFailurePoint point, std::uint64_t nativeCode = 1) noexcept;

    /// @brief Forces the next matching environment operation to fail once.
    void forceNextEnvironmentFailure(EnvironmentFailurePoint point, std::uint64_t nativeCode = 1) noexcept;
} // namespace GameWIP::TestSupport::TestHooks

namespace GameWIP::TestSupport::Detail::TestHooks
{
    /// @brief Atomically consumes a matching one-shot child-process failure.
    [[nodiscard]] std::optional<std::uint64_t> consumeChildProcessFailure(TestSupport::TestHooks::ChildProcessFailurePoint point) noexcept;
    /// @brief Atomically consumes a matching one-shot file failure.
    [[nodiscard]] std::optional<std::uint64_t> consumeFileFailure(TestSupport::TestHooks::FileFailurePoint point) noexcept;
    /// @brief Atomically consumes a matching one-shot environment failure.
    [[nodiscard]] std::optional<std::uint64_t> consumeEnvironmentFailure(TestSupport::TestHooks::EnvironmentFailurePoint point) noexcept;
} // namespace GameWIP::TestSupport::Detail::TestHooks
#endif
