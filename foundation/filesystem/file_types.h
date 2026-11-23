/// @file file_types.h
/// @brief Public file policies and operation options.

#pragma once

#include "filesystem/entry.h"
#include "io/transfer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace GameWIP::FileSystem
{
    /// @brief Default prefix used for same-directory temporary files created by atomic writes.
    /// @note Callers may replace the prefix through Types::File::AtomicWriteOptions; it is UTF-8 filename text, not a path.
    inline constexpr std::string_view kAtomicTemporaryNamePrefix = ".gamewip_tmp_";

    namespace Types
    {
        /// @brief File-handle and whole-file policies.
        namespace File
        {
            /// @brief Access requested for a read/write File handle.
            enum class Access
            {
                /// @brief Open for reading only.
                Read,
                /// @brief Open for writing only.
                Write,
                /// @brief Open for both reading and writing.
                ReadWrite
            };

            /// @brief Bitmask controlling access allowed to other opens while a handle remains open.
            enum class Share : std::uint8_t
            {
                /// @brief Allow no other read, write, or delete opens where the backend can enforce it.
                None = 0,
                /// @brief Allow other handles to request read access.
                Read = 1U << 0U,
                /// @brief Allow other handles to request write access.
                Write = 1U << 1U,
                /// @brief Allow rename, removal, or replacement while this handle remains open.
                Delete = 1U << 2U,
                /// @brief Allow other read and write opens.
                ReadWrite = 0x03U,
                /// @brief Allow other read, write, and delete opens.
                All = 0x07U
            };

            [[nodiscard]] constexpr Share operator|(Share left, Share right) noexcept
            {
                return static_cast<Share>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
            }

            [[nodiscard]] constexpr Share operator&(Share left, Share right) noexcept
            {
                return static_cast<Share>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
            }

            constexpr Share &operator|=(Share &left, Share right) noexcept
            {
                left = left | right;
                return left;
            }

            constexpr Share &operator&=(Share &left, Share right) noexcept
            {
                left = left & right;
                return left;
            }

            /// @brief Creation and truncation behavior for File::open().
            enum class OpenMode
            {
                /// @brief Open only when the file already exists.
                OpenExisting,
                /// @brief Create only when the file does not already exist.
                CreateNew,
                /// @brief Open an existing file or create a missing file without truncating.
                OpenOrCreate,
                /// @brief Open an existing file and truncate it to zero bytes.
                TruncateExisting,
                /// @brief Create a missing file or truncate an existing file.
                CreateOrTruncate
            };

            /// @brief Initial position requested after opening a non-append file.
            enum class InitialPosition
            {
                /// @brief Position the handle at byte zero.
                Beginning,
                /// @brief Position the handle at the current end of the file.
                End
            };

            /// @brief Creation, truncation, and append behavior for FileWriter::open().
            enum class WriterMode
            {
                /// @brief Create only when the file does not already exist.
                CreateNew,
                /// @brief Create a missing file or truncate an existing file.
                CreateOrTruncate,
                /// @brief Open an existing file and truncate it to zero bytes.
                TruncateExisting,
                /// @brief Open an existing file or create a missing file without truncating.
                OpenOrCreate,
                /// @brief Append to an existing file or create a missing file.
                AppendOrCreate,
                /// @brief Append only when the file already exists.
                AppendExisting
            };

            /// @brief Creation and replacement behavior for exact-content whole-file writes.
            enum class WriteMode
            {
                /// @brief Create only when the file does not already exist.
                CreateNew,
                /// @brief Create a missing file or replace existing contents.
                CreateOrTruncate,
                /// @brief Replace contents only when the file already exists.
                TruncateExisting
            };

            /// @brief Creation behavior for append helpers.
            enum class AppendMode
            {
                /// @brief Append to an existing file or create a missing file.
                AppendOrCreate,
                /// @brief Append only when the file already exists.
                AppendExisting
            };
        } // namespace File

        /// @brief Whole-file lock policies and results.
        namespace Lock
        {
            /// @brief Active whole-file lock mode.
            enum class Mode
            {
                /// @brief Shared lock intended to coexist with other shared locks.
                Shared,
                /// @brief Exclusive lock intended to exclude other compatible locks.
                Exclusive
            };

            /// @brief Outcome of a non-blocking lock attempt.
            enum class Outcome
            {
                /// @brief The requested lock was acquired.
                Acquired,
                /// @brief Another lock prevented immediate acquisition.
                WouldBlock
            };

        } // namespace Lock

        namespace File
        {
            /// @brief Metadata copied with file contents.
            enum class CopyMetadataMode
            {
                /// @brief Copy file contents only.
                None,
                /// @brief Copy portable basic metadata such as last-write time and read-only state where supported.
                Basic
            };

            /// @brief Options used by File::open().
            struct OpenOptions
            {
                // Access and initial position
                /// @brief Requested read/write access.
                Access access = Access::ReadWrite;
                /// @brief Creation and truncation behavior.
                OpenMode mode = OpenMode::OpenExisting;
                /// @brief Initial handle position after a successful open.
                InitialPosition initialPosition = InitialPosition::Beginning;

                // Sharing and path resolution
                /// @brief Access allowed to other opens while this handle remains open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;

                // Creation and close behavior
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = false;
                /// @brief Flush requested by explicit close() before releasing the handle.
                IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
            };

            /// @brief Options used by FileReader::open().
            struct ReaderOpenOptions
            {
                /// @brief Access allowed to other opens while this reader remains open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            };

            /// @brief Options used by FileWriter::open().
            struct WriterOpenOptions
            {
                // Write and sharing behavior
                /// @brief Creation, truncation, or append behavior.
                WriterMode mode = WriterMode::CreateOrTruncate;
                /// @brief Access allowed to other opens while this writer remains open.
                Share share = Share::All;

                // Path and close behavior
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = false;
                /// @brief Flush requested by explicit close() before releasing the handle.
                IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
            };

            /// @brief Options used by whole-file read helpers.
            struct ReadOptions
            {
                // Reader policy
                /// @brief Options used to open the underlying reader.
                ReaderOpenOptions open{};

                // Size and transfer limits
                /// @brief Maximum accepted file size, or IO::kNoByteLimit for no caller limit.
                /// @note This is a hard limit, not a successful truncation request.
                std::uint64_t maxBytes = IO::kNoByteLimit;
                /// @brief Temporary transfer buffer size. Must be greater than zero.
                /// @note The buffer is internal to the call and does not limit the final result size.
                std::size_t bufferSize = IO::kDefaultBufferSize;
            };

            /// @brief Options used by exact-content whole-file write helpers.
            struct WriteOptions
            {
                // Destination and access policy
                /// @brief Creation and replacement behavior.
                WriteMode mode = WriteMode::CreateOrTruncate;
                /// @brief Access allowed to other opens while the target is open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;

                // Creation and persistence
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = true;
                /// @brief Flush requested after payload writing and before close.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by append helpers.
            struct AppendOptions
            {
                // Destination and access policy
                /// @brief Whether a missing target is created or reported as NotFound.
                AppendMode mode = AppendMode::AppendOrCreate;
                /// @brief Access allowed to other opens while the target is open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;

                // Creation and persistence
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = true;
                /// @brief Flush requested after payload writing and before close.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by atomic whole-file replacement.
            struct AtomicWriteOptions
            {
                // Destination policy
                /// @brief Create missing parent directories before creating the temporary file.
                bool createParentDirectories = true;
                /// @brief Destination replacement behavior at commit.
                ReplaceMode replaceMode = ReplaceMode::ReplaceExisting;
                /// @brief Symlink traversal policy used to resolve the destination.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;

                // Commit durability
                /// @brief Flush requested for the temporary file before commit.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::Data;
                /// @brief Flush the parent directory after replacement.
                bool flushParentDirectory = true;

                // Temporary-file naming
                /// @brief UTF-8 filename prefix used for a generated temporary file in the destination directory.
                /// @note Must be valid UTF-8, non-empty, contain no path separator or embedded U+0000, and not be "." or "..".
                std::string temporaryNamePrefix{kAtomicTemporaryNamePrefix};
            };

            // Path-based size mutation
            /// @brief Options used by path-based file resizing and truncation.
            struct ResizeOptions
            {
                /// @brief Symlink traversal policy used to resolve the file to mutate.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            };

            // File copy
            /// @brief Options used by copyFile().
            struct CopyOptions
            {
                // Destination and source policy
                /// @brief Destination replacement behavior.
                ReplaceMode replaceMode = ReplaceMode::FailIfExists;
                /// @brief Symlink traversal policy used for source resolution and destination path traversal.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Create missing destination parent directories before copying.
                bool createParentDirectories = false;

                // Metadata and persistence
                /// @brief Additional metadata to copy after file contents.
                CopyMetadataMode metadataMode = CopyMetadataMode::None;
                /// @brief Flush requested for the destination after copying.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };
        } // namespace File
    } // namespace Types
} // namespace GameWIP::FileSystem
