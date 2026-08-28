/// @file files.cpp
/// @brief Filesystem guard and strict UTF-8 text-file implementation for TestSupport.

#include "test_support/files.h"
#include "test_support/internal/test_support_test_hooks.h"
#include "unicode/unicode.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <format>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>

namespace GameWIP::TestSupport
{
    namespace
    {
        std::atomic_uint64_t temporaryDirectoryCounter{0};

        [[nodiscard]] std::uint64_t nativeCode(const std::error_code &error) noexcept
        {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.value()));
        }

        [[nodiscard]] Types::InfrastructureStatus failureStatus(Types::InfrastructureError error, std::uint64_t code = 0) noexcept
        {
            return Types::InfrastructureStatus{.error = error, .nativeCode = code};
        }

        [[nodiscard]] std::string sanitizeTemporaryPurpose(std::string_view purpose)
        {
            std::string sanitized;
            sanitized.reserve(purpose.size());
            for (const char character : purpose)
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

    ScopedTemporaryDirectory::ScopedTemporaryDirectory(std::string_view purpose) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::TemporaryDirectory))
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return;
        }
#endif
        try
        {
            std::error_code error;
            root_ = std::filesystem::temp_directory_path(error);
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                return;
            }
            root_ /= "GameWIP";
            root_ /= "TestSupport";
            static_cast<void>(std::filesystem::create_directories(root_, error));
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                return;
            }

            const std::string prefix = sanitizeTemporaryPurpose(purpose);
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t allocationId = temporaryDirectoryCounter.fetch_add(1, std::memory_order_relaxed);

            for (std::size_t attempt = 0; attempt < 128; ++attempt)
            {
                const std::filesystem::path candidate = root_ / std::format("{}_{:x}_{:x}_{:x}", prefix, ticks, allocationId, attempt);
                error.clear();
                if (std::filesystem::create_directory(candidate, error))
                {
                    path_ = candidate;
                    return;
                }
                if (error && error != std::errc::file_exists)
                {
                    status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                    return;
                }
            }

            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(std::make_error_code(std::errc::file_exists)));
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error.code()));
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    ScopedTemporaryDirectory::~ScopedTemporaryDirectory() noexcept
    {
        try
        {
            std::error_code error;
            if (!path_.empty())
            {
                static_cast<void>(std::filesystem::remove_all(path_, error));
            }
            if (!root_.empty())
            {
                removeDirectoryIfEmpty(root_);
                removeDirectoryIfEmpty(root_.parent_path());
            }
        }
        catch (...) // NOLINT(bugprone-empty-catch) -- Best-effort cleanup cannot propagate.
        {
        }
    }

    const std::filesystem::path &ScopedTemporaryDirectory::path() const noexcept
    {
        return path_;
    }

    // ------------------------------------------------------------
    // Scoped filesystem state
    // ------------------------------------------------------------

    Types::InfrastructureStatus ScopedTemporaryDirectory::status() const noexcept
    {
        return status_;
    }

    ScopedCurrentPath::ScopedCurrentPath(const std::filesystem::path &path) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::CurrentPath))
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return;
        }
#endif
        try
        {
            std::error_code error;
            previousPath_ = std::filesystem::current_path(error);
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                previousPath_.clear();
                return;
            }

            std::filesystem::current_path(path, error);
            if (error)
            {
                status_ = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
                previousPath_.clear();
            }
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
            previousPath_.clear();
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::FileOperationFailed);
            previousPath_.clear();
        }
    }

    ScopedCurrentPath::~ScopedCurrentPath() noexcept
    {
        if (status_.ok())
        {
            try
            {
                std::error_code error;
                std::filesystem::current_path(previousPath_, error);
            }
            catch (...) // NOLINT(bugprone-empty-catch) -- Best-effort restoration cannot propagate.
            {
            }
        }
    }

    const std::filesystem::path &ScopedCurrentPath::previousPath() const noexcept
    {
        return previousPath_;
    }

    Types::InfrastructureStatus ScopedCurrentPath::status() const noexcept
    {
        return status_;
    }

    // ------------------------------------------------------------
    // File operations
    // ------------------------------------------------------------

    Types::TextResult readTextFile(const std::filesystem::path &path) noexcept
    {
        Types::TextResult result;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Read))
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return result;
        }
#endif
        try
        {
            errno = 0;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            const auto endPosition = file.tellg();
            if (endPosition < std::ifstream::pos_type{0})
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }
            if (endPosition == std::ifstream::pos_type{0})
            {
                return result;
            }

            const auto fileSize = static_cast<std::uintmax_t>(endPosition);
            if (fileSize > result.text.max_size())
            {
                result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
                return result;
            }
            if (fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed);
                return result;
            }

            result.text.resize(static_cast<std::size_t>(fileSize));
            file.seekg(0, std::ios::beg);
            if (!file)
            {
                result.text.clear();
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            const std::size_t expectedSize = result.text.size();
            file.read(result.text.data(), static_cast<std::streamsize>(expectedSize));
            const std::size_t bytesRead = static_cast<std::size_t>(file.gcount());
            const bool backendFailure = file.bad();
            const bool knownSizeShortRead = bytesRead != expectedSize;
            result.text.resize(bytesRead);

            const Unicode::Types::Utf8::ValidationResult validation = Unicode::Utf8::validate(result.text);
            if (validation.outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                result.text.resize(validation.validPrefixBytes);
            }

            // Malformed input is definitive regardless of a simultaneous backend failure.
            if (validation.outcome == Unicode::Types::ValidationOutcome::InvalidEncoding)
            {
                result.status = failureStatus(Types::InfrastructureError::EncodingFailed);
                return result;
            }

            // An incomplete suffix is an encoding failure only at a definitive end. A real
            // backend failure remains authoritative because more bytes may have existed.
            if (validation.outcome == Unicode::Types::ValidationOutcome::Incomplete)
            {
                if (backendFailure)
                {
                    result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                }
                else
                {
                    result.status = failureStatus(Types::InfrastructureError::EncodingFailed);
                }
                return result;
            }

            // The size measured before the read is authoritative for this whole-file helper.
            // A valid but shorter read therefore reports an operational failure instead of
            // silently returning a successful prefix.
            if (backendFailure || knownSizeShortRead)
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (const std::length_error &)
        {
            result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error.code()));
            return result;
        }
        catch (...)
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed);
            return result;
        }
    }

    Types::InfrastructureStatus writeTextFile(const std::filesystem::path &path, std::string_view text) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Write))
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
        }
#endif
        try
        {
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return failureStatus(Types::InfrastructureError::InvalidArgument);
            }

            if (Unicode::Utf8::validate(text).outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                return failureStatus(Types::InfrastructureError::EncodingFailed);
            }

            const std::filesystem::path parentPath = path.parent_path();
            if (!parentPath.empty())
            {
                const Types::InfrastructureStatus directoryStatus = createDirectories(parentPath);
                if (!directoryStatus.ok())
                {
                    return directoryStatus;
                }
            }

            errno = 0;
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }

            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();
            if (!file)
            {
                return failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::length_error &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error.code()));
        }
        catch (...)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    Types::BoolResult fileExists(const std::filesystem::path &path) noexcept
    {
        Types::BoolResult result;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Exists))
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
            return result;
        }
#endif
        try
        {
            std::error_code error;
            result.value = std::filesystem::exists(path, error);
            if (error)
            {
                result.status = failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error));
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (...)
        {
            result.status = failureStatus(Types::InfrastructureError::FileOperationFailed);
            return result;
        }
    }

    Types::BoolResult fileContains(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);
        Types::BoolResult result{.status = readResult.status};
        if (result.status.ok())
        {
            result.value = readResult.text.find(text) != std::string::npos;
        }
        return result;
    }

    Types::CountResult countFileOccurrences(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);
        Types::CountResult result{.status = readResult.status};
        if (!result.status.ok() || text.empty())
        {
            return result;
        }

        std::size_t position = 0;
        while ((position = readResult.text.find(text, position)) != std::string::npos)
        {
            ++result.count;
            position += text.size();
        }
        return result;
    }

    Types::InfrastructureStatus createDirectories(const std::filesystem::path &path) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::CreateDirectories))
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
        }
#endif
        if (path.empty())
        {
            return {};
        }

        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::create_directories(path, error));
            return error ? failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error)) : Types::InfrastructureStatus{};
        }
        catch (const std::bad_alloc &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    Types::InfrastructureStatus removeIfExists(const std::filesystem::path &path) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = Detail::TestHooks::consumeFileFailure(TestHooks::FileFailurePoint::Remove))
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed, *injected);
        }
#endif
        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove_all(path, error));
            return error ? failureStatus(Types::InfrastructureError::FileOperationFailed, nativeCode(error)) : Types::InfrastructureStatus{};
        }
        catch (const std::bad_alloc &)
        {
            return failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            return failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }
} // namespace GameWIP::TestSupport
