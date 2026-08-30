/// @file path.h
/// @brief Public path vocabulary and operations for GameWIP FileSystem.

#pragma once

#include "io/status.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace GameWIP::FileSystem
{
    namespace Types
    {
        /// @brief Project-wide filesystem path spelling.
        /// @details This is a naming alias, not a portable path-grammar or ABI abstraction over std::filesystem::path.
        using Path = std::filesystem::path;

        /// @brief Generic boolean query result.
        struct BoolResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Boolean result when status is successful.
            bool value = false;
        };

        /// @brief Result returned by path-producing operations.
        struct PathResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Resulting path when status is successful.
            Path path;
        };

        /// @brief Result returned when converting a Path to UTF-8.
        struct Utf8PathResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief UTF-8 path text when status is successful.
            std::string utf8;
        };
    } // namespace Types

    /// @brief Returns the process current working directory.
    /// @return Current working directory path or a query failure status.
    [[nodiscard]] Types::PathResult getCurrentDirectory() noexcept;

    /// @brief Sets the process current working directory.
    /// @param path Existing directory to make current.
    /// @return Success or a validation, lookup, type, permission, or native failure status.
    [[nodiscard]] IO::Types::Status setCurrentDirectory(const Types::Path &path) noexcept;

    /// @brief Returns the parent component of a path.
    /// @param path Path to inspect.
    /// @return Parent path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult parentPath(const Types::Path &path) noexcept;

    /// @brief Returns the filename component of a path.
    /// @param path Path to inspect.
    /// @return Filename path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult filename(const Types::Path &path) noexcept;

    /// @brief Returns the stem component of a path filename.
    /// @param path Path to inspect.
    /// @return Stem path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult stem(const Types::Path &path) noexcept;

    /// @brief Returns the extension component of a path filename.
    /// @param path Path to inspect.
    /// @return Extension path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult extension(const Types::Path &path) noexcept;

    /// @brief Returns a copy of a path with its extension replaced.
    /// @param path Source path.
    /// @param newExtension Replacement extension, with or without a leading period.
    /// @return Updated path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult replaceExtension(const Types::Path &path, const Types::Path &newExtension) noexcept;

    /// @brief Joins two path parts using std::filesystem::path component rules.
    /// @param left Base path.
    /// @param right Path component to append. A rooted right-hand path may replace part or all of left.
    /// @return Joined path or a conversion/allocation failure status.
    /// @note This is lexical and does not access, normalize, or canonicalize the filesystem.
    [[nodiscard]] Types::PathResult joinPath(const Types::Path &left, const Types::Path &right) noexcept;

    /// @brief Tests whether a path is absolute according to platform path rules.
    /// @param path Path to inspect.
    /// @return Successful boolean result or a conversion/allocation failure status.
    [[nodiscard]] Types::BoolResult isAbsolutePath(const Types::Path &path) noexcept;

    /// @brief Tests whether a path is relative according to platform path rules.
    /// @param path Path to inspect.
    /// @return Successful boolean result or a conversion/allocation failure status.
    [[nodiscard]] Types::BoolResult isRelativePath(const Types::Path &path) noexcept;

    /// @brief Converts a path to an absolute path using the process current directory when needed.
    /// @param path Non-empty path to resolve.
    /// @return Absolute path or a query/conversion/allocation failure status.
    /// @note This does not require the target to exist and does not promise canonical or normalized spelling.
    [[nodiscard]] Types::PathResult absolutePath(const Types::Path &path) noexcept;

    /// @brief Resolves a canonical path whose complete target must exist.
    /// @param path Non-empty path to resolve.
    /// @return Canonical path or a lookup, permission, conversion, or allocation failure status.
    /// @note Uses ordinary std::filesystem symlink resolution and does not apply SymlinkPolicy.
    [[nodiscard]] Types::PathResult canonicalPath(const Types::Path &path) noexcept;

    /// @brief Resolves a best-effort canonical path that permits missing trailing components.
    /// @param path Non-empty path to resolve.
    /// @return Weakly canonical path or a permission, conversion, or allocation failure status.
    /// @note Uses ordinary std::filesystem symlink resolution and does not apply SymlinkPolicy.
    [[nodiscard]] Types::PathResult weaklyCanonicalPath(const Types::Path &path) noexcept;

    /// @brief Returns the platform temporary-directory path.
    /// @return Temporary-directory path or a query/conversion/allocation failure status.
    [[nodiscard]] Types::PathResult getTemporaryDirectoryPath() noexcept;

    /// @brief Converts UTF-8 text to the platform-native Path representation.
    /// @param utf8Path UTF-8 path text.
    /// @return Converted path or EncodingFailed/OutOfMemory.
    /// @note Conversion does not make the path absolute, canonical, or normalized.
    [[nodiscard]] Types::PathResult pathFromUtf8(std::string_view utf8Path) noexcept;

    /// @brief Converts a Path's stored spelling to UTF-8 text.
    /// @param path Path to convert.
    /// @return UTF-8 path text or EncodingFailed/OutOfMemory.
    /// @note The result does not promise normalized or platform-independent separators.
    [[nodiscard]] Types::Utf8PathResult pathToUtf8(const Types::Path &path) noexcept;
} // namespace GameWIP::FileSystem
