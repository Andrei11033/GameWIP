/// @file progress_dialog_state.h
/// @brief Private portable state for ProgressDialog.

#pragma once

#include "desktop/dialogs.h"
#include "desktop/types.h"

#include <memory>

namespace GameWIP::Desktop::Detail::Platform
{
    struct ProgressDialogData;

    struct ProgressDialogDataDeleter
    {
        void operator()(ProgressDialogData *data) const noexcept;
    };
} // namespace GameWIP::Desktop::Detail::Platform

namespace GameWIP::Desktop::Detail
{
    struct ProgressDialogState
    {
        std::unique_ptr<Platform::ProgressDialogData, Platform::ProgressDialogDataDeleter> platform;
        ProgressDialogState *deferredCleanupNext = nullptr;
        ProgressDialogState *pendingOwnerRestoreNext = nullptr;
        std::uint64_t ownerToken = 0;
        Types::WindowId ownerId;
        Types::Dialogs::Progress::Mode mode = Types::Dialogs::Progress::Mode::Indeterminate;
        double progress = 0.0;
        bool cancelable = false;
        bool blocksOwner = true;
        bool cancelRequested = false;
        bool nativeDestroyedPendingFinalize = false;
        bool ownerRestorePending = false;
    };

    struct ProgressDialogAccess
    {
        [[nodiscard]] static ProgressDialogState *state(ProgressDialog &dialog) noexcept
        {
            return dialog.state_.get();
        }

        [[nodiscard]] static const ProgressDialogState *state(const ProgressDialog &dialog) noexcept
        {
            return dialog.state_.get();
        }
    };
} // namespace GameWIP::Desktop::Detail
