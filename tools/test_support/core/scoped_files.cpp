/// @file scoped_files.cpp
/// @brief Filesystem guard implementation for TestSupport.

#include "test_support/files.h"
#include "test_support/internal/status.h"
#include "test_support/process.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <system_error>

namespace GameWIP::TestSupport
{
    namespace
    {
        std::atomic_uint64_t temporaryDirectoryCounter{0};

        [[nodiscard]] std::string sanitizeTemporaryPurpose(std::string_view purpose)
        {
            std::string sanitized;
            sanitized.reserve(purpose.size());

            for(const char character : purpose)
            {
                const bool asciiLetter = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
                const bool asciiDigit = character >= '0' && character <= '9';
                sanitized.push_back(asciiLetter || asciiDigit || character == '-' || character == '_' ? character : '_');
            }

            return sanitized.empty() ? "test" : sanitized;
        }

        void removeDirectoryIfEmpty(const std::filesystem::path &path) noexcept
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove(path, error));
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Scoped filesystem state
    /// ------------------------------------------------------------

    ScopedTemporaryDirectory::ScopedTemporaryDirectory(std::string_view purpose) noexcept
    {
        try
        {
            std::error_code error;
            root_ = std::filesystem::temp_directory_path(error);

            if(error)
            {
                status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                return;
            }
            // Keep TestSupport-owned temporary trees under a predictable parent so
            // cleanup can remove empty parent directories without touching unrelated temp data.
            root_ /= "GameWIP";
            root_ /= "TestSupport";
            static_cast<void>(std::filesystem::create_directories(root_, error));

            if(error)
            {
                status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                return;
            }
            const std::string prefix = sanitizeTemporaryPurpose(purpose);
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t allocationId = temporaryDirectoryCounter.fetch_add(1, std::memory_order_relaxed);

            // The timestamp gives readable names; the atomic counter and bounded retry
            // handle same-tick construction and rare filesystem collisions.
            for(std::size_t attempt = 0; attempt < 128; ++attempt)
            {
                const std::filesystem::path candidate = root_ / std::format("{}_{:x}_{:x}_{:x}", prefix, ticks, allocationId, attempt);
                error.clear();

                if(std::filesystem::create_directory(candidate, error))
                {
                    path_ = candidate;
                    return;
                }

                if(error && error != std::errc::file_exists)
                {
                    status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                    return;
                }
            }

            status_ = Internal::failureStatus(
                Types::InfrastructureError::FileOperationFailed,
                Internal::nativeCode(std::make_error_code(std::errc::file_exists)));
        }
        catch(const std::bad_alloc &)
        {
            status_ = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch(const std::filesystem::filesystem_error &error)
        {
            status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error.code()));
        }
        catch(...)
        {
            status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    ScopedTemporaryDirectory::~ScopedTemporaryDirectory() noexcept
    {
        try
        {
            std::error_code error;

            if(!path_.empty())
            {
                static_cast<void>(std::filesystem::remove_all(path_, error));
            }

            if(!root_.empty())
            {
                removeDirectoryIfEmpty(root_);
                removeDirectoryIfEmpty(root_.parent_path());
            }
        }
        catch(...) // NOLINT(bugprone-empty-catch) -- Best-effort cleanup cannot propagate.
        {
        }
    }

    const std::filesystem::path &ScopedTemporaryDirectory::path() const noexcept
    {
        return path_;
    }

    Types::InfrastructureStatus ScopedTemporaryDirectory::status() const noexcept
    {
        return status_;
    }

    ScopedCurrentPath::ScopedCurrentPath(ScopedProcessState &processState, const std::filesystem::path &path) noexcept
        : processState_(&processState)
    {
        try
        {
            // Require the process-state scope before changing the shared directory.

            if(!processState.status().ok())
            {
                status_ = processState.status();
                return;
            }

            std::error_code error;
            previousPath_ = std::filesystem::current_path(error);

            if(error)
            {
                status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                previousPath_.clear();
                return;
            }

            // Resolve the destination before changing the process current directory.

            const std::filesystem::path targetPath = std::filesystem::absolute(path, error);

            if(error)
            {
                status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                previousPath_.clear();
                return;
            }

            expectedPath_ = targetPath;
            std::filesystem::current_path(targetPath, error);

            if(error)
            {
                status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                previousPath_.clear();
                return;
            }
        }
        catch(const std::bad_alloc &)
        {
            status_ = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
            previousPath_.clear();
        }
        catch(...)
        {
            status_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
            previousPath_.clear();
        }
    }

    ScopedCurrentPath::~ScopedCurrentPath() noexcept
    {
        static_cast<void>(restore());
    }

    Types::InfrastructureStatus ScopedCurrentPath::restore() noexcept
    {
        if(!status_.ok())
        {
            return status_;
        }

        if(restored_)
        {
            return restorationStatus_;
        }

        try
        {
            // Read the current directory before deciding whether this guard can restore it.

            std::error_code error;
            const std::filesystem::path currentPath = std::filesystem::current_path(error);

            if(error)
            {
                restorationStatus_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
                processState_->recordRestorationFailure(restorationStatus_);
                return restorationStatus_;
            }

            if(currentPath != expectedPath_)
            {
                restorationStatus_ = Internal::failureStatus(Types::InfrastructureError::ProcessStateConflict);
                processState_->recordRestorationFailure(restorationStatus_);
                return restorationStatus_;
            }
            std::filesystem::current_path(previousPath_, error);
            restorationStatus_ = error ? Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error))
                                       : Types::InfrastructureStatus{};
        }
        catch(const std::bad_alloc &)
        {
            restorationStatus_ = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch(...) // NOLINT(bugprone-empty-catch) -- Restoration failures are returned through the explicit method.
        {
            restorationStatus_ = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
        }

        restored_ = restorationStatus_.ok();

        if(!restorationStatus_.ok())
        {
            processState_->recordRestorationFailure(restorationStatus_);
        }

        return restorationStatus_;
    }

    const std::filesystem::path &ScopedCurrentPath::previousPath() const noexcept
    {
        return previousPath_;
    }

    Types::InfrastructureStatus ScopedCurrentPath::status() const noexcept
    {
        return status_;
    }

} // namespace GameWIP::TestSupport
