/// @file files.h
/// @brief Filesystem fixtures and strict UTF-8 text-file helpers for TestSupport.

#pragma once

#include "test_support/types.h"

#include <filesystem>
#include <string_view>

namespace GameWIP::TestSupport
{
    /// @brief Owns one unique directory beneath the process temporary directory.
    /// @details The validation runner scopes the process temporary directory to its active preset; standalone consumers use the host default.
    /// @details Construction is non-throwing and leaves an inert guard on failure. Destruction performs best-effort recursive cleanup.
    class ScopedTemporaryDirectory
    {
    public:
        /// @brief Attempts to create a unique temporary directory using purpose as a readable filename prefix.
        explicit ScopedTemporaryDirectory(std::string_view purpose = "test") noexcept;
        /// @brief Best-effort removes the owned directory tree and empty TestSupport parent directories.
        ~ScopedTemporaryDirectory() noexcept;

        ScopedTemporaryDirectory(const ScopedTemporaryDirectory &) = delete;            ///< Ownership cannot be copied.
        ScopedTemporaryDirectory &operator=(const ScopedTemporaryDirectory &) = delete; ///< Ownership cannot be copy-assigned.
        ScopedTemporaryDirectory(ScopedTemporaryDirectory &&) = delete;                 ///< Ownership cannot be moved.
        ScopedTemporaryDirectory &operator=(ScopedTemporaryDirectory &&) = delete;      ///< Ownership cannot be move-assigned.

        /// @brief Returns the owned temporary directory path.
        [[nodiscard]] const std::filesystem::path &path() const noexcept;
        /// @brief Returns the construction status; a failed guard owns no path.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::filesystem::path path_;         ///< Owned temporary directory.
        std::filesystem::path root_;         ///< TestSupport temporary root used for best-effort parent cleanup.
        Types::InfrastructureStatus status_; ///< Construction status.
    };

    /// @brief Temporarily changes the process current directory and restores it on destruction.
    /// @note Current-directory state is process-global; callers must coordinate overlapping use.
    class ScopedCurrentPath
    {
    public:
        /// @brief Captures the current directory and attempts to change it to path.
        explicit ScopedCurrentPath(const std::filesystem::path &path) noexcept;
        /// @brief Best-effort restores the captured current directory.
        ~ScopedCurrentPath() noexcept;

        ScopedCurrentPath(const ScopedCurrentPath &) = delete;            ///< Ownership cannot be copied.
        ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete; ///< Ownership cannot be copy-assigned.
        ScopedCurrentPath(ScopedCurrentPath &&) = delete;                 ///< Ownership cannot be moved.
        ScopedCurrentPath &operator=(ScopedCurrentPath &&) = delete;      ///< Ownership cannot be move-assigned.

        /// @brief Returns the directory that a successful guard will restore.
        [[nodiscard]] const std::filesystem::path &previousPath() const noexcept;
        /// @brief Returns the construction status; a failed guard performs no restoration.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::filesystem::path previousPath_; ///< Captured process current directory.
        Types::InfrastructureStatus status_; ///< Construction status.
    };

    /// @brief Reads one complete file as strict UTF-8 text.
    /// @return Status plus valid UTF-8 text. Encoding failure may preserve the complete valid prefix.
    /// @note No normalization, BOM handling, or newline conversion is performed.
    [[nodiscard]] Types::TextResult readTextFile(const std::filesystem::path &path) noexcept;

    /// @brief Creates parent directories and writes one complete strict UTF-8 text file in binary truncate mode.
    /// @param path Destination path.
    /// @param text Valid UTF-8 bytes to write unchanged.
    /// @return Success or an explicit infrastructure failure.
    /// @note UTF-8 is validated before parent creation or a truncating destination open.
    [[nodiscard]] Types::InfrastructureStatus writeTextFile(const std::filesystem::path &path, std::string_view text) noexcept;

    /// @brief Queries whether path exists without collapsing inspection failure into absence.
    [[nodiscard]] Types::BoolResult fileExists(const std::filesystem::path &path) noexcept;
    /// @brief Searches strict UTF-8 file text for a UTF-8 substring and preserves the read status.
    [[nodiscard]] Types::BoolResult fileContains(const std::filesystem::path &path, std::string_view text) noexcept;
    /// @brief Counts non-overlapping UTF-8 substring occurrences and preserves the read status.
    [[nodiscard]] Types::CountResult countFileOccurrences(const std::filesystem::path &path, std::string_view text) noexcept;
    /// @brief Creates a directory tree; an empty path is a successful no-op.
    [[nodiscard]] Types::InfrastructureStatus createDirectories(const std::filesystem::path &path) noexcept;
    /// @brief Removes a file or complete directory tree; absence is successful.
    [[nodiscard]] Types::InfrastructureStatus removeIfExists(const std::filesystem::path &path) noexcept;
} // namespace GameWIP::TestSupport
