/// @file files.cpp
/// @brief Strict UTF-8 text-file operations for TestSupport.

#include "test_support/files.h"
#include "test_support/internal/status.h"
#include "unicode/unicode.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>

namespace GameWIP::TestSupport
{
    /// ------------------------------------------------------------
    /// File operations
    /// ------------------------------------------------------------

    Types::TextResult readTextFile(const std::filesystem::path &path) noexcept
    {
        Types::TextResult result;
        try
        {
            // Measure before reading so a short read cannot silently succeed.
            errno = 0;
            std::ifstream file(path, std::ios::binary | std::ios::ate);

            if(!file.is_open())
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            const auto endPosition = file.tellg();

            if(endPosition < std::ifstream::pos_type{0})
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            if(endPosition == std::ifstream::pos_type{0})
            {
                return result;
            }

            const auto fileSize = static_cast<std::uintmax_t>(endPosition);

            if(fileSize > result.text.max_size())
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
                return result;
            }

            if(fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
                return result;
            }

            // Read only the measured file contents.
            result.text.resize(static_cast<std::size_t>(fileSize));
            file.seekg(0, std::ios::beg);

            if(!file)
            {
                result.text.clear();
                result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                return result;
            }

            const std::size_t expectedSize = result.text.size();
            file.read(result.text.data(), static_cast<std::streamsize>(expectedSize));
            const std::size_t bytesRead = static_cast<std::size_t>(file.gcount());
            const bool backendFailure = file.bad();
            const bool knownSizeShortRead = bytesRead != expectedSize;
            result.text.resize(bytesRead);

            // Validate text independently of the file operation status.
            const Unicode::Types::Utf8::ValidationResult validation = Unicode::Utf8::validate(result.text);

            if(validation.outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                result.text.resize(validation.validPrefixBytes);
            }

            // Malformed input is definitive regardless of a simultaneous backend failure.
            if(validation.outcome == Unicode::Types::ValidationOutcome::InvalidEncoding)
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::EncodingFailed);
                return result;
            }

            // An incomplete suffix is an encoding failure only at a definitive end. A real
            // backend failure remains authoritative because more bytes may have existed.
            if(validation.outcome == Unicode::Types::ValidationOutcome::Incomplete)
            {
                if(backendFailure)
                {
                    result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
                }
                else
                {
                    result.status = Internal::failureStatus(Types::InfrastructureError::EncodingFailed);
                }

                return result;
            }

            // The size measured before the read is authoritative for this whole-file helper.
            // A valid but shorter read therefore reports an operational failure instead of
            // silently returning a successful prefix.
            if(backendFailure || knownSizeShortRead)
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch(const std::length_error &)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch(const std::filesystem::filesystem_error &error)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error.code()));
            return result;
        }
        catch(...)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
            return result;
        }
    }

    Types::InfrastructureStatus writeTextFile(const std::filesystem::path &path, std::string_view text) noexcept
    {
        try
        {
            // Validate input before creating parent directories or truncating the destination.
            if(text.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return Internal::failureStatus(Types::InfrastructureError::InvalidArgument);
            }

            if(Unicode::Utf8::validate(text).outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                return Internal::failureStatus(Types::InfrastructureError::EncodingFailed);
            }

            // Create parent directories after the input is known to be valid.
            const std::filesystem::path parentPath = path.parent_path();

            if(!parentPath.empty())
            {
                const Types::InfrastructureStatus directoryStatus = createDirectories(parentPath);

                if(!directoryStatus.ok())
                {
                    return directoryStatus;
                }
            }

            // Write the validated UTF-8 bytes without text-mode conversion.
            errno = 0;
            std::ofstream file(path, std::ios::binary | std::ios::trunc);

            if(!file.is_open())
            {
                return Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();

            if(!file)
            {
                return Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, static_cast<std::uint64_t>(errno));
            }
            return {};
        }
        catch(const std::bad_alloc &)
        {
            return Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch(const std::length_error &)
        {
            return Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch(const std::filesystem::filesystem_error &error)
        {
            return Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error.code()));
        }
        catch(...)
        {
            return Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    Types::BoolResult fileExists(const std::filesystem::path &path) noexcept
    {
        Types::BoolResult result;
        try
        {
            std::error_code error;
            result.value = std::filesystem::exists(path, error);

            if(error)
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error));
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch(...)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
            return result;
        }
    }

    Types::BoolResult fileContains(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);
        Types::BoolResult result{.status = readResult.status};

        if(result.status.ok())
        {
            result.value = readResult.text.contains(text);
        }

        return result;
    }

    Types::CountResult countFileOccurrences(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);
        Types::CountResult result{.status = readResult.status};

        if(!result.status.ok() || text.empty())
        {
            return result;
        }

        std::size_t position = 0;

        while((position = readResult.text.find(text, position)) != std::string::npos)
        {
            ++result.count;
            position += text.size();
        }

        return result;
    }

    Types::InfrastructureStatus createDirectories(const std::filesystem::path &path) noexcept
    {
        if(path.empty())
        {
            return {};
        }

        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::create_directories(path, error));
            return error ? Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error))
                         : Types::InfrastructureStatus{};
        }
        catch(const std::bad_alloc &)
        {
            return Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch(...)
        {
            return Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }

    Types::InfrastructureStatus removeIfExists(const std::filesystem::path &path) noexcept
    {
        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove_all(path, error));
            return error ? Internal::failureStatus(Types::InfrastructureError::FileOperationFailed, Internal::nativeCode(error))
                         : Types::InfrastructureStatus{};
        }
        catch(const std::bad_alloc &)
        {
            return Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch(...)
        {
            return Internal::failureStatus(Types::InfrastructureError::FileOperationFailed);
        }
    }
} // namespace GameWIP::TestSupport
