/// @file whole_file.cpp
/// @brief FileSystem checked whole-file byte and UTF-8 text operations.

#include "filesystem/internal/helpers.h"

#include <cstddef>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Internal;

    namespace
    {
        [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeFlushAndClose(
            FileWriter &writer,
            std::span<const std::byte> bytes,
            IO::Types::FlushMode flushMode) noexcept
        {
            auto result = IO::writeAllBytes(writer, bytes);

            if(!result.ok())
            {
                // Preserve the payload failure if best-effort cleanup also fails.
                static_cast<void>(writer.close());
                return result;
            }

            auto flushResult = writer.flush(flushMode);

            if(!flushResult.ok())
            {
                return GameWIP::Status::Types::Result<IO::Types::WriteProgress>::failure(
                    flushResult.status(), std::move(result).value());
            }

            auto closeResult = writer.close();

            if(!closeResult.ok())
            {
                return GameWIP::Status::Types::Result<IO::Types::WriteProgress>::failure(
                    closeResult.status(), std::move(result).value());
            }

            return GameWIP::Status::Types::Result<IO::Types::WriteProgress>::success(std::move(result).value());
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Whole-file reads
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<std::vector<std::byte>> readAllBytes(const Types::Path &path, const Types::File::ReadOptions &options) noexcept
    {
        try
        {
            if(options.bufferSize == 0)
            {
                return GameWIP::Status::Types::Result<std::vector<std::byte>>::failure(
                    GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            FileReader reader;
            auto openResult = reader.open(path, options.open);

            if(!openResult.ok())
            {
                return GameWIP::Status::Types::Result<std::vector<std::byte>>::failure(openResult.status());
            }

            auto result = IO::readAllBytes(reader, options.maxBytes, options.bufferSize);
            auto closeResult = reader.close();

            if(result.ok() && !closeResult.ok())
            {
                return GameWIP::Status::Types::Result<std::vector<std::byte>>::failure(
                    closeResult.status(), std::move(result).value());
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<std::vector<std::byte>>::failure(
                GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<std::vector<std::byte>>::failure(
                GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<std::string> readAllText(const Types::Path &path, const Types::File::ReadOptions &options) noexcept
    {
        try
        {
            if(options.bufferSize == 0)
            {
                return GameWIP::Status::Types::Result<std::string>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            FileReader reader;
            auto openResult = reader.open(path, options.open);

            if(!openResult.ok())
            {
                return GameWIP::Status::Types::Result<std::string>::failure(openResult.status());
            }

            auto result = IO::readAllText(reader, options.maxBytes, options.bufferSize);
            auto closeResult = reader.close();

            if(result.ok() && !closeResult.ok())
            {
                return GameWIP::Status::Types::Result<std::string>::failure(closeResult.status(), std::move(result).value());
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<std::string>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<std::string>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    /// ------------------------------------------------------------
    /// Whole-file writes
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<IO::Types::WriteProgress>
    writeAllBytes(const Types::Path &path, std::span<const std::byte> bytes, const Types::File::WriteOptions &options) noexcept
    {
        try
        {
            if(!IO::isValidFlushMode(options.flushMode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return writeFailure(ErrorCode::InvalidArgument);
            }

            Types::File::WriterMode writerMode = Types::File::WriterMode::CreateOrTruncate;

            switch(options.mode)
            {
            case Types::File::WriteMode::CreateNew:
                writerMode = Types::File::WriterMode::CreateNew;
                break;
            case Types::File::WriteMode::CreateOrTruncate:
                writerMode = Types::File::WriterMode::CreateOrTruncate;
                break;
            case Types::File::WriteMode::TruncateExisting:
                writerMode = Types::File::WriterMode::TruncateExisting;
                break;
            default:
                return writeFailure(ErrorCode::InvalidArgument);
            }

            FileWriter writer;
            auto openResult = writer.open(
                path,
                Types::File::WriterOpenOptions{
                    .mode = writerMode,
                    .share = options.share,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = options.createParentDirectories,
                    .flushOnClose = IO::Types::FlushMode::None});

            if(!openResult.ok())
            {
                return writeFailure(openResult.status());
            }

            // Write exact contents, then apply flush and close.
            return writeFlushAndClose(writer, bytes, options.flushMode);
        }
        catch(const std::bad_alloc &)
        {
            return writeFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return writeFailure(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress>
    writeAllText(const Types::Path &path, std::string_view utf8Text, const Types::File::WriteOptions &options) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return writeFailure(ErrorCode::EncodingFailed);
        }

        return writeAllBytes(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }

    /// ------------------------------------------------------------
    /// Whole-file appends
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<IO::Types::WriteProgress>
    appendBytes(const Types::Path &path, std::span<const std::byte> bytes, const Types::File::AppendOptions &options) noexcept
    {
        try
        {
            if(!IO::isValidFlushMode(options.flushMode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return writeFailure(ErrorCode::InvalidArgument);
            }

            Types::File::WriterMode writerMode = Types::File::WriterMode::AppendOrCreate;

            switch(options.mode)
            {
            case Types::File::AppendMode::AppendOrCreate:
                writerMode = Types::File::WriterMode::AppendOrCreate;
                break;
            case Types::File::AppendMode::AppendExisting:
                writerMode = Types::File::WriterMode::AppendExisting;
                break;
            default:
                return writeFailure(ErrorCode::InvalidArgument);
            }

            FileWriter writer;
            auto openResult = writer.open(
                path,
                Types::File::WriterOpenOptions{
                    .mode = writerMode,
                    .share = options.share,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = options.createParentDirectories,
                    .flushOnClose = IO::Types::FlushMode::None});

            if(!openResult.ok())
            {
                return writeFailure(openResult.status());
            }

            // Append the payload, then apply flush and close.
            return writeFlushAndClose(writer, bytes, options.flushMode);
        }
        catch(const std::bad_alloc &)
        {
            return writeFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return writeFailure(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress>
    appendText(const Types::Path &path, std::string_view utf8Text, const Types::File::AppendOptions &options) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return writeFailure(ErrorCode::EncodingFailed);
        }

        return appendBytes(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }

} // namespace GameWIP::FileSystem
