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
        using GameWIP::Status::Types::Code;
        using GameWIP::Status::Types::Result;

        std::string text;
        bool hasPartialText = false;
        const auto failure = [&text, &hasPartialText](Types::Status status)
        {
            return hasPartialText ? Result<std::string>::failure(std::move(status), std::move(text))
                                  : Result<std::string>::failure(std::move(status));
        };

        try
        {
            // Measure before reading so a short read cannot silently succeed.
            errno = 0;
            std::ifstream file(path, std::ios::binary | std::ios::ate);

            if(!file.is_open())
            {
                return failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }

            const auto endPosition = file.tellg();

            if(endPosition < std::ifstream::pos_type{0})
            {
                return failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }

            if(endPosition == std::ifstream::pos_type{0})
            {
                return Result<std::string>::success({});
            }

            const auto fileSize = static_cast<std::uintmax_t>(endPosition);

            if(fileSize > text.max_size())
            {
                return failure(Internal::failureStatus(Code::OutOfMemory));
            }

            if(fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return failure(Internal::failureStatus(Code::FileOperationFailed));
            }

            // Read only the measured file contents.
            text.resize(static_cast<std::size_t>(fileSize));
            file.seekg(0, std::ios::beg);

            if(!file)
            {
                text.clear();
                return failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }

            const std::size_t expectedSize = text.size();
            file.read(text.data(), static_cast<std::streamsize>(expectedSize));
            const std::size_t bytesRead = static_cast<std::size_t>(file.gcount());
            const bool backendFailure = file.bad();
            const bool knownSizeShortRead = bytesRead != expectedSize;
            text.resize(bytesRead);
            hasPartialText = true;

            // Validate text independently of the file operation status.
            const Unicode::Types::Utf8::ValidationResult validation = Unicode::Utf8::validate(text);

            if(validation.outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                text.resize(validation.validPrefixBytes);
            }

            // Malformed input is definitive regardless of a simultaneous backend failure.
            if(validation.outcome == Unicode::Types::ValidationOutcome::InvalidEncoding)
            {
                return failure(Internal::failureStatus(Code::EncodingFailed));
            }

            // A short or failed read cannot prove that an incomplete suffix belongs to the file.
            if(validation.outcome == Unicode::Types::ValidationOutcome::Incomplete)
            {
                if(backendFailure || knownSizeShortRead)
                {
                    return failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
                }

                return failure(Internal::failureStatus(Code::EncodingFailed));
            }

            // The measured size is authoritative, so a valid short prefix is still a read failure.
            if(backendFailure || knownSizeShortRead)
            {
                return failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }

            return Result<std::string>::success(std::move(text));
        }
        catch(const std::bad_alloc &)
        {
            return failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(const std::length_error &)
        {
            return failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(const std::filesystem::filesystem_error &error)
        {
            return failure(Internal::failureStatus(Code::FileOperationFailed, Internal::nativeCode(error.code())));
        }
        catch(...)
        {
            return failure(Internal::failureStatus(Code::FileOperationFailed));
        }
    }

    Types::Result<void> writeTextFile(const std::filesystem::path &path, std::string_view text) noexcept
    {
        using GameWIP::Status::Types::Code;

        try
        {
            // Validate input before creating parent directories or truncating the destination.
            if(text.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::InvalidArgument));
            }

            if(Unicode::Utf8::validate(text).outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::EncodingFailed));
            }

            // Create parent directories after the input is known to be valid.
            const std::filesystem::path parentPath = path.parent_path();

            if(!parentPath.empty())
            {
                const Types::Result<void> directoryResult = createDirectories(parentPath);

                if(!directoryResult.ok())
                {
                    return directoryResult;
                }
            }

            // Write the validated UTF-8 bytes without text-mode conversion.
            errno = 0;
            std::ofstream file(path, std::ios::binary | std::ios::trunc);

            if(!file.is_open())
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();

            if(!file)
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }

            // Report delayed filesystem errors from closing the destination.
            errno = 0;
            file.close();

            if(file.fail())
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::errnoCode(errno)));
            }

            return Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(const std::length_error &)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(const std::filesystem::filesystem_error &error)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::nativeCode(error.code())));
        }
        catch(...)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed));
        }
    }

    Types::BoolResult fileExists(const std::filesystem::path &path) noexcept
    {
        using GameWIP::Status::Types::Code;
        using GameWIP::Status::Types::Result;

        try
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);

            if(error)
            {
                return Result<bool>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::nativeCode(error)));
            }

            return Result<bool>::success(exists);
        }
        catch(const std::bad_alloc &)
        {
            return Result<bool>::failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<bool>::failure(Internal::failureStatus(Code::FileOperationFailed));
        }
    }

    Types::BoolResult fileContains(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);

        if(!readResult.ok())
        {
            return Types::BoolResult::failure(readResult.status());
        }

        return Types::BoolResult::success(readResult.value().contains(text));
    }

    Types::CountResult countFileOccurrences(const std::filesystem::path &path, std::string_view text) noexcept
    {
        Types::TextResult readResult = readTextFile(path);

        if(!readResult.ok())
        {
            return Types::CountResult::failure(readResult.status());
        }

        if(text.empty())
        {
            return Types::CountResult::success(0);
        }

        std::size_t count = 0;
        std::size_t position = 0;

        while((position = readResult.value().find(text, position)) != std::string::npos)
        {
            ++count;
            position += text.size();
        }

        return Types::CountResult::success(count);
    }

    Types::Result<void> createDirectories(const std::filesystem::path &path) noexcept
    {
        using GameWIP::Status::Types::Code;

        if(path.empty())
        {
            return Types::Result<void>::success();
        }

        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::create_directories(path, error));

            if(error)
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::nativeCode(error)));
            }

            return Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(...)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed));
        }
    }

    Types::Result<void> removeIfExists(const std::filesystem::path &path) noexcept
    {
        using GameWIP::Status::Types::Code;

        try
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove_all(path, error));

            if(error)
            {
                return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed, Internal::nativeCode(error)));
            }

            return Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::OutOfMemory));
        }
        catch(...)
        {
            return Types::Result<void>::failure(Internal::failureStatus(Code::FileOperationFailed));
        }
    }
} // namespace GameWIP::TestSupport
