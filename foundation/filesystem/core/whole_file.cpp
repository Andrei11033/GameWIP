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
        [[nodiscard]] IO::Types::WriteResult writeFlushAndClose(
            FileWriter &writer,
            std::span<const std::byte> bytes,
            IO::Types::FlushMode flushMode) noexcept
        {
            IO::Types::WriteResult result = IO::writeAllBytes(writer, bytes);

            if(!result.status.ok())
            {
                // Preserve the payload failure if best-effort cleanup also fails.
                static_cast<void>(writer.close());
                return result;
            }

            IO::Types::Status flushStatus = writer.flush(flushMode);

            if(!flushStatus.ok())
            {
                result.status = std::move(flushStatus);
                return result;
            }

            IO::Types::Status closeStatus = writer.close();

            if(!closeStatus.ok())
            {
                result.status = std::move(closeStatus);
            }

            return result;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Whole-file reads
    /// ------------------------------------------------------------

    IO::Types::ReadAllBytesResult readAllBytes(const Types::Path &path, const Types::File::ReadOptions &options) noexcept
    {
        try
        {
            if(options.bufferSize == 0)
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            FileReader reader;
            IO::Types::Status openStatus = reader.open(path, options.open);

            if(!openStatus.ok())
            {
                return {.status = std::move(openStatus)};
            }

            IO::Types::ReadAllBytesResult result = IO::readAllBytes(reader, options.maxBytes, options.bufferSize);
            IO::Types::Status closeStatus = reader.close();

            if(result.status.ok() && !closeStatus.ok())
            {
                result.status = std::move(closeStatus);
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::ReadAllTextResult readAllText(const Types::Path &path, const Types::File::ReadOptions &options) noexcept
    {
        try
        {
            if(options.bufferSize == 0)
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            FileReader reader;
            IO::Types::Status openStatus = reader.open(path, options.open);

            if(!openStatus.ok())
            {
                return {.status = std::move(openStatus)};
            }

            IO::Types::ReadAllTextResult result = IO::readAllText(reader, options.maxBytes, options.bufferSize);
            IO::Types::Status closeStatus = reader.close();

            if(result.status.ok() && !closeStatus.ok())
            {
                result.status = std::move(closeStatus);
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    /// ------------------------------------------------------------
    /// Whole-file writes
    /// ------------------------------------------------------------

    IO::Types::WriteResult writeAllBytes(const Types::Path &path, std::span<const std::byte> bytes, const Types::File::WriteOptions &options) noexcept
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
            IO::Types::Status openStatus = writer.open(
                path,
                Types::File::WriterOpenOptions{
                    .mode = writerMode,
                    .share = options.share,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = options.createParentDirectories,
                    .flushOnClose = IO::Types::FlushMode::None});

            if(!openStatus.ok())
            {
                return writeFailure(std::move(openStatus));
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

    IO::Types::WriteResult writeAllText(const Types::Path &path, std::string_view utf8Text, const Types::File::WriteOptions &options) noexcept
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

    IO::Types::WriteResult appendBytes(const Types::Path &path, std::span<const std::byte> bytes, const Types::File::AppendOptions &options) noexcept
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
            IO::Types::Status openStatus = writer.open(
                path,
                Types::File::WriterOpenOptions{
                    .mode = writerMode,
                    .share = options.share,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = options.createParentDirectories,
                    .flushOnClose = IO::Types::FlushMode::None});

            if(!openStatus.ok())
            {
                return writeFailure(std::move(openStatus));
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

    IO::Types::WriteResult appendText(const Types::Path &path, std::string_view utf8Text, const Types::File::AppendOptions &options) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return writeFailure(ErrorCode::EncodingFailed);
        }

        return appendBytes(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }

} // namespace GameWIP::FileSystem
