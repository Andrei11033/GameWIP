/// @file dialogs.h
/// @brief Portable backend contract for dialogs.

#pragma once

#include "desktop/dialogs.h"
#include "desktop/internal/progress_dialog_state.h"

#include <memory>
#include <string_view>

namespace GameWIP::Desktop::Internal
{
    struct WindowState;
}

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shared portable state
    /// ------------------------------------------------------------

    using Internal::ProgressDialogState;
    using Internal::WindowState;

    /// @brief Result of closing an owner-thread progress dialog.
    struct ProgressCloseResult
    {
        IO::Types::Status status;
        bool resourceClosed = false;
    };

    /// ------------------------------------------------------------
    /// One-shot dialogs
    /// ------------------------------------------------------------

    /// @brief Opens a native single-file picker.
    [[nodiscard]] Types::Dialogs::File::Result openFile(const Types::Dialogs::File::OpenDescription &description) noexcept;
    /// @brief Opens a native multi-file picker.
    [[nodiscard]] Types::Dialogs::File::ListResult openFiles(const Types::Dialogs::File::OpenDescription &description) noexcept;
    /// @brief Opens a native save-file picker.
    [[nodiscard]] Types::Dialogs::File::Result saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept;
    /// @brief Opens a native folder picker.
    [[nodiscard]] Types::Dialogs::File::Result selectFolder(const Types::Dialogs::File::FolderDescription &description) noexcept;
    /// @brief Opens a native multi-folder picker.
    [[nodiscard]] Types::Dialogs::File::ListResult selectFolders(const Types::Dialogs::File::FolderDescription &description) noexcept;
    /// @brief Shows a native message dialog.
    [[nodiscard]] Types::Dialogs::Message::Result showMessage(const Types::Dialogs::Message::Description &description) noexcept;
    /// @brief Shows a native text prompt.
    [[nodiscard]] Types::Dialogs::Prompt::Result showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept;

    /// ------------------------------------------------------------
    /// Progress dialog lifecycle
    /// ------------------------------------------------------------

    /// @brief Opens a native progress dialog and binds it to its owner thread.
    [[nodiscard]] IO::Types::Status openProgress(ProgressDialogState &state, const Types::Dialogs::Progress::Description &description) noexcept;
    /// @brief Closes a native progress dialog and reports native ownership.
    [[nodiscard]] ProgressCloseResult closeProgress(ProgressDialogState &state) noexcept;
    /// @brief Attempts owner-thread progress cleanup without publishing errors.
    [[nodiscard]] bool closeProgressBestEffort(ProgressDialogState &state) noexcept;
    /// @brief Transfers wrong-thread progress cleanup to its owner dispatcher.
    [[nodiscard]] bool deferProgressCleanupToOwner(std::unique_ptr<ProgressDialogState> &state) noexcept;
    /// @brief Finalizes progress state during owner-dispatcher teardown.
    void finalizeProgressForDispatcherExit(ProgressDialogState &state) noexcept;
    /// @brief Restores owner interaction after a progress dialog closes.
    [[nodiscard]] IO::Types::Status notifyProgressOwnerLoss(WindowState &owner) noexcept;
    /// @brief Best-effort owner interaction restoration for teardown paths.
    void notifyProgressOwnerLossBestEffort(WindowState &owner) noexcept;
    /// @brief Tests whether a Window is blocked by a live progress dialog.
    [[nodiscard]] bool windowHasBlockingProgressDialog(const WindowState &window) noexcept;

    /// ------------------------------------------------------------
    /// Progress dialog updates
    /// ------------------------------------------------------------

    [[nodiscard]] IO::Types::Status setProgressTitle(ProgressDialogState &state, std::string_view title) noexcept;
    [[nodiscard]] IO::Types::Status setProgressHeading(ProgressDialogState &state, std::string_view heading) noexcept;
    [[nodiscard]] IO::Types::Status setProgressMessage(ProgressDialogState &state, std::string_view message) noexcept;
    [[nodiscard]] IO::Types::Status setProgressMode(ProgressDialogState &state, Types::Dialogs::Progress::Mode mode) noexcept;
    [[nodiscard]] IO::Types::Status setProgressValue(ProgressDialogState &state, double progress) noexcept;

} // namespace GameWIP::Desktop::Platform
