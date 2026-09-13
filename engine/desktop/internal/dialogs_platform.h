/// @file dialogs_platform.h
/// @brief Internal portable-to-native dialog backend contract.

#pragma once

#include "desktop/dialogs.h"
#include "desktop/internal/progress_dialog_state.h"

namespace GameWIP::Desktop::TestHooks
{
    struct NativePixelRect;
    struct ProgressDialogNativeSnapshot;
} // namespace GameWIP::Desktop::TestHooks

namespace GameWIP::Desktop::Detail
{
    struct WindowState;
}

namespace GameWIP::Desktop::Detail::Platform
{
    struct ProgressCloseResult
    {
        IO::Types::Status status;
        bool resourceClosed = false;
    };

    [[nodiscard]] Types::Dialogs::File::Result openFile(const Types::Dialogs::File::OpenDescription &description) noexcept;
    [[nodiscard]] Types::Dialogs::File::ListResult openFiles(const Types::Dialogs::File::OpenDescription &description) noexcept;
    [[nodiscard]] Types::Dialogs::File::Result saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept;
    [[nodiscard]] Types::Dialogs::File::Result selectFolder(const Types::Dialogs::File::FolderDescription &description) noexcept;
    [[nodiscard]] Types::Dialogs::File::ListResult selectFolders(const Types::Dialogs::File::FolderDescription &description) noexcept;
    [[nodiscard]] Types::Dialogs::Message::Result showMessage(const Types::Dialogs::Message::Description &description) noexcept;
    [[nodiscard]] Types::Dialogs::Prompt::Result showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept;

    [[nodiscard]] IO::Types::Status openProgress(ProgressDialogState &state, const Types::Dialogs::Progress::Description &description) noexcept;
    [[nodiscard]] ProgressCloseResult closeProgress(ProgressDialogState &state) noexcept;
    [[nodiscard]] bool closeProgressBestEffort(ProgressDialogState &state) noexcept;
    [[nodiscard]] bool deferProgressCleanupToOwner(std::unique_ptr<ProgressDialogState> &state) noexcept;
    void finalizeProgressForDispatcherExit(ProgressDialogState &state) noexcept;
    [[nodiscard]] IO::Types::Status notifyProgressOwnerLoss(WindowState &owner) noexcept;
    void notifyProgressOwnerLossBestEffort(WindowState &owner) noexcept;
    [[nodiscard]] bool windowHasBlockingProgressDialog(const WindowState &window) noexcept;

    [[nodiscard]] IO::Types::Status setProgressTitle(ProgressDialogState &state, std::string_view title) noexcept;
    [[nodiscard]] IO::Types::Status setProgressHeading(ProgressDialogState &state, std::string_view heading) noexcept;
    [[nodiscard]] IO::Types::Status setProgressMessage(ProgressDialogState &state, std::string_view message) noexcept;
    [[nodiscard]] IO::Types::Status setProgressMode(ProgressDialogState &state, Types::Dialogs::Progress::Mode mode) noexcept;
    [[nodiscard]] IO::Types::Status setProgressValue(ProgressDialogState &state, double progress) noexcept;

#if DESKTOP_INTERNAL_TEST_HOOKS
    [[nodiscard]] IO::Types::Status testDialogApartment() noexcept;
    [[nodiscard]] TestHooks::ProgressDialogNativeSnapshot inspectProgressDialog(const ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status requestProgressDialogCancel(ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status requestProgressDialogClose(ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status destroyNativeProgressDialog(ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status simulateProgressDialogDpiChange(
        ProgressDialogState *state,
        TestHooks::NativePixelRect suggestedBounds,
        std::uint32_t dpi) noexcept;
    [[nodiscard]] std::size_t activeProgressDialogCount() noexcept;
    [[nodiscard]] std::size_t deferredProgressDialogCount() noexcept;
    [[nodiscard]] std::size_t progressDialogClassReferenceCount() noexcept;
    [[nodiscard]] bool progressOwnerRestoreMessageRegistrationAttempted() noexcept;
#endif
} // namespace GameWIP::Desktop::Detail::Platform
