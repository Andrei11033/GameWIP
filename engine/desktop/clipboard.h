/// @file clipboard.h
/// @brief Portable synchronous operating-system Clipboard service.

#pragma once

#include "status/result.h"
#include "desktop/data_transfer.h"
#include "desktop/desktop_export.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Types::Clipboard
{
    /// @brief Observable publication progress after a Clipboard write attempt.
    enum class CommitState
    {
        NotStarted,         ///< The previous Clipboard contents were not cleared.
        Cleared,            ///< The Clipboard was cleared but no requested item was published.
        PartiallyPublished, ///< A proper prefix of requested items was published.
        Published           ///< Every requested item was published.
    };

    /// @brief Exact external mutation progress for a write attempt.
    struct WriteProgress
    {
        CommitState commitState = CommitState::NotStarted; ///< Highest completed external mutation state.
        std::size_t formatsPublished = 0;                  ///< Successfully published caller-order prefix length.
    };

    /// @brief External mutation state for a clear attempt.
    struct ClearProgress
    {
        bool cleared = false; ///< True once the native clear has succeeded.
    };
} // namespace GameWIP::Desktop::Types::Clipboard

/// @brief Stateless synchronous access to the desktop/process Clipboard.
/// @details Operations need no open GameWIP Window and may be called from ordinary application
/// threads. Timeouts bound GameWIP-controlled Clipboard acquisition; an already-entered native read
/// may still synchronously wait for an external delayed-rendering owner.
namespace GameWIP::Desktop::Clipboard
{
    /// @brief Requests exactly one non-waiting Clipboard acquisition attempt.
    inline constexpr std::chrono::milliseconds kNoWait{0};
    /// @brief Default bounded Clipboard acquisition timeout used by convenience overloads.
    inline constexpr std::chrono::milliseconds kDefaultAccessTimeout{100};

    /// @brief Tests a format using the default access timeout.
    /// @param format Valid format description; standard categories require an empty customName.
    /// @return Shared result; a successful false value means ordinary absence.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<bool> hasFormat(Types::DataTransfer::FormatView format) noexcept;
    /// @brief Tests a format using an explicit bounded access timeout.
    /// @param format Valid format description; custom names are strict non-empty UTF-8.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result; a successful false value means ordinary absence.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<bool> hasFormat(
        Types::DataTransfer::FormatView format,
        std::chrono::milliseconds timeout) noexcept;

    /// @brief Enumerates formats using the default timeout.
    /// @return Shared result with owning formats; failed enumeration may retain a partial list.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::vector<Types::DataTransfer::Format>> getFormats() noexcept;
    /// @brief Enumerates formats using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result with owning formats; failed enumeration may retain a partial list.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::vector<Types::DataTransfer::Format>> getFormats(
        std::chrono::milliseconds timeout) noexcept;

    /// @brief Reads strict UTF-8 text using the default timeout.
    /// @return `Result<string>`; absence reports NotFound.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::string> readText() noexcept;
    /// @brief Reads strict UTF-8 text using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Text and status; malformed native Unicode reports EncodingFailed.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::string> readText(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an ordered native path list using the default timeout.
    /// @return Shared result containing paths; absence reports NotFound.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::vector<FileSystem::Types::Path>> readFiles() noexcept;
    /// @brief Reads an ordered native path list using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Paths and status without performing file-system IO.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::vector<FileSystem::Types::Path>> readFiles(
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads a tightly packed RGBA8 image using the default timeout.
    /// @return `Result<Image>`; an available unconvertible native image reports Unsupported.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::DataTransfer::Image> readImage() noexcept;
    /// @brief Reads a tightly packed RGBA8 image using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return `Result<Image>`; absence reports NotFound.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::DataTransfer::Image> readImage(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an opaque named native block using the default timeout.
    /// @param formatName Non-empty strict UTF-8 native format name without embedded U+0000.
    /// @return Shared result containing opaque bytes; absence reports NotFound.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::vector<std::byte>> readCustomData(std::string_view formatName) noexcept;
    /// @brief Reads an opaque named native block using an explicit timeout.
    /// @param formatName Non-empty strict UTF-8 native format name without embedded U+0000.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result containing opaque bytes; logical framing remains format-defined.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<std::vector<std::byte>> readCustomData(
        std::string_view formatName,
        std::chrono::milliseconds timeout) noexcept;

    /// @brief Replaces Clipboard contents with UTF-8 text using the default timeout.
    /// @param text Valid UTF-8 without embedded U+0000; empty text is valid.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeText(std::string_view text) noexcept;
    /// @brief Replaces Clipboard contents with UTF-8 text using an explicit timeout.
    /// @param text Valid UTF-8 without embedded U+0000; empty text is valid.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeText(
        std::string_view text,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with absolute paths using the default timeout.
    /// @param paths Non-empty absolute native paths; existence is not queried.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeFiles(
        std::span<const FileSystem::Types::Path> paths) noexcept;
    /// @brief Replaces Clipboard contents with absolute paths using an explicit timeout.
    /// @param paths Non-empty absolute native paths; existence is not queried.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeFiles(
        std::span<const FileSystem::Types::Path> paths,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with an RGBA8 image using the default timeout.
    /// @param image Valid sRGB top-to-bottom straight-alpha image view.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeImage(
        const Types::DataTransfer::ImageView &image) noexcept;
    /// @brief Replaces Clipboard contents with an RGBA8 image using an explicit timeout.
    /// @param image Valid sRGB top-to-bottom straight-alpha image view.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeImage(
        const Types::DataTransfer::ImageView &image,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with one named block using the default timeout.
    /// @param data Valid custom name and opaque payload; Win32 requires non-empty bytes and
    /// returns Unsupported before mutation for an empty payload.
    /// @return Shared result; progress remains available if publication or cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeCustomData(
        const Types::DataTransfer::CustomView &data) noexcept;
    /// @brief Replaces Clipboard contents with one named block using an explicit timeout.
    /// @param data Valid custom name and opaque payload; Win32 requires non-empty bytes and
    /// returns Unsupported before mutation for an empty payload.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status and exact publication progress.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeCustomData(
        const Types::DataTransfer::CustomView &data,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Transactionally prepares and publishes ordered formats using the default timeout.
    /// @param items Non-empty item views with no duplicate native identities; Win32 custom items
    /// require non-empty bytes and return Unsupported before mutation otherwise.
    /// @return Shared result with committed state and published prefix, including on failure.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> write(
        std::span<const Types::DataTransfer::ItemView> items) noexcept;
    /// @brief Transactionally prepares and publishes ordered formats using an explicit timeout.
    /// @param items Non-empty item views with no duplicate native identities; Win32 custom items
    /// require non-empty bytes and return Unsupported before mutation otherwise.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result with committed state and published prefix, including on failure.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> write(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept;

    /// @brief Clears Clipboard contents using the default timeout.
    /// @return Shared result; clear progress remains available if cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::ClearProgress> clear() noexcept;
    /// @brief Clears Clipboard contents using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Shared result; clear progress remains available if cleanup fails.
    [[nodiscard]] DESKTOP_EXPORT GameWIP::Status::Types::Result<Types::Clipboard::ClearProgress> clear(std::chrono::milliseconds timeout) noexcept;
} // namespace GameWIP::Desktop::Clipboard
