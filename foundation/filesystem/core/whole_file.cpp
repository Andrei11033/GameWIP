/// @file whole_file.cpp
/// @brief FileSystem checked whole-file byte and UTF-8 text operations.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/filesystem_platform.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Detail;

    namespace
    {
        // Atomic temporary-name validation

        [[nodiscard]] bool hasPathSeparator(std::string_view text) noexcept
        {
            return text.contains('/') || text.contains('\\') || text.contains('\0');
        }

        [[nodiscard]] IO::Types::Status validateAtomicTemporaryPrefix(std::string_view prefix) noexcept
        {
            if(!validUtf8(prefix))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed);
            }

            if(prefix.empty() || prefix == "." || prefix == ".." || hasPathSeparator(prefix))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            return IO::successStatus();
        }

        // Parent-directory validation

        [[nodiscard]] IO::Types::Status validateDirectoryExists(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
        {
            Platform::EntryQueryResult result = queryEntry(path, symlinkPolicy);

            if(!result.status.ok())
            {
                return std::move(result.status);
            }

            if(result.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::NotDirectory);
            }

            return IO::successStatus();
        }

        // Same-directory temporary-path generation

        [[nodiscard]] Types::Path uniqueAtomicTemporaryPath(const Types::Path &parent, std::string_view prefix, std::uint64_t attempt)
        {
            static std::atomic<std::uint64_t> counter{0};

            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed);
            std::string name;
            name.reserve(prefix.size() + 64);
            name.append(prefix);
            name.append(std::to_string(static_cast<std::uint64_t>(ticks)));
            name.push_back('_');
            name.append(std::to_string(value));
            name.push_back('_');
            name.append(std::to_string(attempt));
            name.append(".tmp");

            return parent / pathFromTrustedUtf8(name);
        }

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

    /// ------------------------------------------------------------
    /// Atomic replacement
    /// ------------------------------------------------------------

    IO::Types::Status writeAllBytesAtomic(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::File::AtomicWriteOptions &options) noexcept
    {
        try
        {
            // Validate replacement options before touching the destination.
            if(path.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
               !IO::isValidFlushMode(options.flushMode))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            IO::Types::Status prefixStatus = validateAtomicTemporaryPrefix(options.temporaryNamePrefix);

            if(!prefixStatus.ok())
            {
                return prefixStatus;
            }

            Types::Path parent = path.parent_path();

            if(parent.empty())
            {
                parent = ".";
            }

            // Ensure the parent exists before checking the destination.
            IO::Types::Status parentStatus =
                options.createParentDirectories
                    ? createDirectories(
                          parent,
                          Types::Directory::CreateOptions{.succeedIfAlreadyExists = true, .symlinkPolicy = options.symlinkPolicy})
                    : validateDirectoryExists(parent, options.symlinkPolicy);

            if(!parentStatus.ok())
            {
                return parentStatus;
            }

            // Reject conflicts and unsupported symlink replacement before creating a temporary.
            Detail::Platform::EntryQueryResult destination = queryEntry(path, options.symlinkPolicy);

            if(destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }

            if(!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
            {
                return std::move(destination.status);
            }

            if(destination.status.ok() && options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            Types::Path temporaryPath;
            IO::Types::Status openStatus = IO::makeStatus(ErrorCode::AlreadyExists);
            FileWriter writer;

            // Stage the replacement in a same-directory temporary with bounded collision retries.
            for(std::uint64_t attempt = 0; attempt < 64 && openStatus.code == ErrorCode::AlreadyExists; ++attempt)
            {
                temporaryPath = uniqueAtomicTemporaryPath(parent, options.temporaryNamePrefix, attempt);
                openStatus = writer.open(
                    temporaryPath,
                    Types::File::WriterOpenOptions{
                        .mode = Types::File::WriterMode::CreateNew,
                        .share = Types::File::Share::None,
                        .symlinkPolicy = options.symlinkPolicy,
                        .createParentDirectories = false,
                        .flushOnClose = IO::Types::FlushMode::None});
            }

            if(!openStatus.ok())
            {
                return openStatus;
            }

            // Write, flush, and close the temporary before replacing the destination.
            IO::Types::WriteResult writeResult = IO::writeAllBytes(writer, bytes);

            if(!writeResult.status.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return std::move(writeResult.status);
            }

            IO::Types::Status flushStatus = writer.flush(options.flushMode);

            if(!flushStatus.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return flushStatus;
            }

            IO::Types::Status closeStatus = writer.close();

            if(!closeStatus.ok())
            {
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return closeStatus;
            }

            // Publish the completed temporary with the selected replacement policy.
            IO::Types::Status commitStatus = Detail::Platform::movePath(temporaryPath, path, options.replaceMode, options.symlinkPolicy);

            if(!commitStatus.ok())
            {
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return commitStatus;
            }

            // The replacement is committed; a parent-flush failure is post-commit.
            if(options.flushParentDirectory)
            {
                IO::Types::Status parentFlushStatus = Detail::Platform::flushDirectory(parent);

                if(!parentFlushStatus.ok())
                {
                    return parentFlushStatus;
                }
            }

            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status writeAllTextAtomic(const Types::Path &path, std::string_view utf8Text, const Types::File::AtomicWriteOptions &options) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return IO::makeStatus(ErrorCode::EncodingFailed);
        }

        return writeAllBytesAtomic(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }
} // namespace GameWIP::FileSystem
