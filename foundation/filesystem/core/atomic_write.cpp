/// @file atomic_write.cpp
/// @brief FileSystem atomic replacement operations.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

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
    using namespace Internal;

    namespace
    {
        // Atomic temporary-name validation

        [[nodiscard]] bool hasPathSeparator(std::string_view text) noexcept
        {
            return text.contains('/') || text.contains('\\') || text.contains('\0');
        }

        [[nodiscard]] GameWIP::Status::Types::Result<void> validateAtomicTemporaryPrefix(std::string_view prefix) noexcept
        {
            if(!validUtf8(prefix))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::EncodingFailed));
            }

            if(prefix.empty() || prefix == "." || prefix == ".." || hasPathSeparator(prefix))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            return GameWIP::Status::Types::Result<void>::success();
        }

        // Parent-directory validation

        [[nodiscard]] GameWIP::Status::Types::Result<void>
        validateDirectoryExists(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
        {
            auto result = queryEntry(path, symlinkPolicy);

            if(!result.ok())
            {
                return GameWIP::Status::Types::Result<void>::failure(result.status());
            }

            if(result.value().kind != Types::EntryKind::Directory)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::NotDirectory));
            }

            return GameWIP::Status::Types::Result<void>::success();
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
    } // namespace

    /// ------------------------------------------------------------
    /// Atomic replacement
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> writeAllBytesAtomic(
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
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            auto prefixStatus = validateAtomicTemporaryPrefix(options.temporaryNamePrefix);

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
            auto parentStatus =
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
            auto destination = queryEntry(path, options.symlinkPolicy);

            if(destination.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyExists));
            }

            if(!destination.ok() && destination.status().code() != ErrorCode::NotFound)
            {
                return GameWIP::Status::Types::Result<void>::failure(destination.status());
            }

            if(destination.ok() && options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unsupported));
            }

            Types::Path temporaryPath;
            auto openResult = GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyExists));
            FileWriter writer;

            // Stage the replacement in a same-directory temporary with bounded collision retries.
            for(std::uint64_t attempt = 0; attempt < 64 && openResult.status().code() == ErrorCode::AlreadyExists; ++attempt)
            {
                temporaryPath = uniqueAtomicTemporaryPath(parent, options.temporaryNamePrefix, attempt);
                openResult = writer.open(
                    temporaryPath,
                    Types::File::WriterOpenOptions{
                        .mode = Types::File::WriterMode::CreateNew,
                        .share = Types::File::Share::None,
                        .symlinkPolicy = options.symlinkPolicy,
                        .createParentDirectories = false,
                        .flushOnClose = IO::Types::FlushMode::None});
            }

            if(!openResult.ok())
            {
                return openResult;
            }

            // Write, flush, and close the temporary before replacing the destination.
            auto writeResult = IO::writeAllBytes(writer, bytes);

            if(!writeResult.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return GameWIP::Status::Types::Result<void>::failure(writeResult.status());
            }

            auto flushResult = writer.flush(options.flushMode);

            if(!flushResult.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return flushResult;
            }

            auto closeResult = writer.close();

            if(!closeResult.ok())
            {
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return closeResult;
            }

            // Publish the completed temporary with the selected replacement policy.
            auto commitResult = Platform::movePath(temporaryPath, path, options.replaceMode, options.symlinkPolicy);

            if(!commitResult.ok())
            {
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return commitResult;
            }

            // The replacement is committed; a parent-flush failure is post-commit.
            if(options.flushParentDirectory)
            {
                auto parentFlushResult = Platform::flushDirectory(parent);

                if(!parentFlushResult.ok())
                {
                    return parentFlushResult;
                }
            }

            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<void>
    writeAllTextAtomic(const Types::Path &path, std::string_view utf8Text, const Types::File::AtomicWriteOptions &options) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::EncodingFailed));
        }

        return writeAllBytesAtomic(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }
} // namespace GameWIP::FileSystem
