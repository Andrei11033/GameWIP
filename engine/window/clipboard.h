/// @file clipboard.h
/// @brief Portable synchronous operating-system Clipboard service.

#pragma once

#include "io/io.h"
#include "window/data_transfer.h"
#include "window/window_export.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Window::Types::Clipboard
{
    /// @brief Observable publication progress after a Clipboard write attempt.
    enum class CommitState
    {
        NotStarted,         ///< The previous Clipboard contents were not cleared.
        Cleared,            ///< The Clipboard was cleared but no requested item was published.
        PartiallyPublished, ///< A proper prefix of requested items was published.
        Published           ///< Every requested item was published.
    };

    /// @brief Status and format-availability predicate result.
    struct FormatResult
    {
        IO::Types::Status status; ///< Query status; absence itself is successful.
        bool available = false;   ///< Whether the valid requested format is available.
    };

    /// @brief Status and owning ordered format enumeration.
    struct FormatsResult
    {
        IO::Types::Status status;                  ///< Enumeration status.
        std::vector<DataTransfer::Format> formats; ///< Meaningful formats materialized before completion/failure.
    };

    /// @brief Status and UTF-8 Clipboard text.
    struct TextResult
    {
        IO::Types::Status status; ///< Read status.
        std::string text;         ///< UTF-8 text on success.
    };

    /// @brief Status and ordered Clipboard file list.
    struct FileListResult
    {
        IO::Types::Status status;                   ///< Read status.
        std::vector<FileSystem::Types::Path> paths; ///< Materialized native paths on success.
    };

    /// @brief Status and tightly packed Clipboard image.
    struct ImageResult
    {
        IO::Types::Status status;  ///< Read status.
        DataTransfer::Image image; ///< Materialized RGBA8 image on success.
    };

    /// @brief Status and opaque native custom-format block.
    struct CustomDataResult
    {
        IO::Types::Status status;     ///< Read status; absence is NotFound.
        std::vector<std::byte> bytes; ///< Complete native allocation extent on success.
    };

    /// @brief Status and exact external mutation progress for a write.
    struct WriteResult
    {
        IO::Types::Status status;                          ///< Preparation, access, publication, or cleanup status.
        CommitState commitState = CommitState::NotStarted; ///< Highest completed external mutation state.
        std::size_t formatsPublished = 0;                  ///< Successfully published caller-order prefix length.
    };

    /// @brief Status and external mutation state for clear.
    struct ClearResult
    {
        IO::Types::Status status; ///< Access, clear, or cleanup status.
        bool cleared = false;     ///< True once the native clear has succeeded.
    };
} // namespace GameWIP::Window::Types::Clipboard

/// @brief Stateless synchronous access to the desktop/process Clipboard.
/// @details Operations need no open GameWIP Window and may be called from ordinary application
/// threads. Timeouts bound GameWIP-controlled Clipboard acquisition; an already-entered native read
/// may still synchronously wait for an external delayed-rendering owner.
namespace GameWIP::Window::Clipboard
{
    /// @brief Requests exactly one non-waiting Clipboard acquisition attempt.
    inline constexpr std::chrono::milliseconds kNoWait{0};
    /// @brief Default bounded Clipboard acquisition timeout used by convenience overloads.
    inline constexpr std::chrono::milliseconds kDefaultAccessTimeout{100};

    /// @brief Tests a format using the default access timeout.
    /// @param format Valid format description; standard categories require an empty customName.
    /// @return Successful false for ordinary absence, or an operational failure status.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::FormatResult hasFormat(Types::DataTransfer::FormatView format) noexcept;
    /// @brief Tests a format using an explicit bounded access timeout.
    /// @param format Valid format description; custom names are strict nonempty UTF-8.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Successful false for ordinary absence, or an operational failure status.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::FormatResult hasFormat(
        Types::DataTransfer::FormatView format,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Enumerates formats using the default timeout.
    /// @return Status plus owning descriptions in meaningful native priority order.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::FormatsResult getFormats() noexcept;
    /// @brief Enumerates formats using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status plus any meaningful owning prefix materialized before failure.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::FormatsResult getFormats(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads strict UTF-8 text using the default timeout.
    /// @return Text and status; absence reports NotFound.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::TextResult readText() noexcept;
    /// @brief Reads strict UTF-8 text using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Text and status; malformed native Unicode reports EncodingFailed.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::TextResult readText(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an ordered native path list using the default timeout.
    /// @return Paths and status; absence reports NotFound.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::FileListResult readFiles() noexcept;
    /// @brief Reads an ordered native path list using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Paths and status without performing file-system IO.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::FileListResult readFiles(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads a tightly packed RGBA8 image using the default timeout.
    /// @return Image and status; an available unconvertible native image reports Unsupported.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::ImageResult readImage() noexcept;
    /// @brief Reads a tightly packed RGBA8 image using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Image and status; absence reports NotFound.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::ImageResult readImage(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an opaque named native block using the default timeout.
    /// @param formatName Nonempty strict UTF-8 native format name without embedded U+0000.
    /// @return Native allocation extent and status; absence reports NotFound.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::CustomDataResult readCustomData(std::string_view formatName) noexcept;
    /// @brief Reads an opaque named native block using an explicit timeout.
    /// @param formatName Nonempty strict UTF-8 native format name without embedded U+0000.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Native allocation extent and status; logical framing remains format-defined.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::CustomDataResult readCustomData(
        std::string_view formatName,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with UTF-8 text using the default timeout.
    /// @param text Valid UTF-8 without embedded U+0000; empty text is valid.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeText(std::string_view text) noexcept;
    /// @brief Replaces Clipboard contents with UTF-8 text using an explicit timeout.
    /// @param text Valid UTF-8 without embedded U+0000; empty text is valid.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeText(std::string_view text, std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with absolute paths using the default timeout.
    /// @param paths Nonempty absolute native paths; existence is not queried.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeFiles(std::span<const FileSystem::Types::Path> paths) noexcept;
    /// @brief Replaces Clipboard contents with absolute paths using an explicit timeout.
    /// @param paths Nonempty absolute native paths; existence is not queried.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeFiles(
        std::span<const FileSystem::Types::Path> paths,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with an RGBA8 image using the default timeout.
    /// @param image Valid sRGB top-to-bottom straight-alpha image view.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeImage(const Types::DataTransfer::ImageView &image) noexcept;
    /// @brief Replaces Clipboard contents with an RGBA8 image using an explicit timeout.
    /// @param image Valid sRGB top-to-bottom straight-alpha image view.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeImage(
        const Types::DataTransfer::ImageView &image,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Replaces Clipboard contents with one named block using the default timeout.
    /// @param data Valid custom name and opaque payload.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeCustomData(const Types::DataTransfer::CustomView &data) noexcept;
    /// @brief Replaces Clipboard contents with one named block using an explicit timeout.
    /// @param data Valid custom name and opaque payload.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status and exact publication progress.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult writeCustomData(
        const Types::DataTransfer::CustomView &data,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Transactionally prepares and publishes ordered formats using the default timeout.
    /// @param items Nonempty item views with no duplicate native identities.
    /// @return Status, committed state, and successfully published caller-order prefix length.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult write(std::span<const Types::DataTransfer::ItemView> items) noexcept;
    /// @brief Transactionally prepares and publishes ordered formats using an explicit timeout.
    /// @param items Nonempty item views with no duplicate native identities.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status, committed state, and successfully published caller-order prefix length.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::WriteResult write(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Clears Clipboard contents using the default timeout.
    /// @return Status and whether the external clear completed.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::ClearResult clear() noexcept;
    /// @brief Clears Clipboard contents using an explicit timeout.
    /// @param timeout Zero for one attempt, positive for bounded retry, or invalid when negative.
    /// @return Status and whether the clear completed, even when close later fails.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Clipboard::ClearResult clear(std::chrono::milliseconds timeout) noexcept;
} // namespace GameWIP::Window::Clipboard
