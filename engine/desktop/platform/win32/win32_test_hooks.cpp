/// @file win32_test_hooks.cpp
/// @brief Source-tree-only test adapter for Win32-specific Desktop hooks.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/test_hooks.h"

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/progress_dialog_state.h"
#include "desktop/internal/window_state.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace
{
    namespace Win32TestHooks = GameWIP::Desktop::Internal::TestHooks::Win32;
    namespace Win32Backend = GameWIP::Desktop::Internal::Win32;

    /// ------------------------------------------------------------
    /// Dialog fixture storage
    /// ------------------------------------------------------------

    thread_local std::optional<std::pair<Win32TestHooks::FileDialogOperation, Win32TestHooks::FileDialogResponse>> nextFileDialogResponse;
    thread_local Win32TestHooks::FileDialogSnapshot fileDialogSnapshot;
    thread_local std::optional<Win32TestHooks::MessageDialogResponse> nextMessageDialogResponse;
    thread_local Win32TestHooks::MessageDialogSnapshot messageDialogSnapshot;
    thread_local std::optional<Win32TestHooks::PromptDialogResponse> nextPromptDialogResponse;
    thread_local Win32TestHooks::PromptDialogSnapshot promptDialogSnapshot;
} // namespace

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Dialog fixture bridge
    /// ------------------------------------------------------------

    bool consumeFileDialogResponse(Win32TestHooks::FileDialogOperation operation, Win32TestHooks::FileDialogResponse &response) noexcept
    {
        if(!nextFileDialogResponse || nextFileDialogResponse->first != operation)
        {
            return false;
        }

        response = std::move(nextFileDialogResponse->second);
        nextFileDialogResponse.reset();
        return true;
    }

    void recordFileDialogSnapshot(Win32TestHooks::FileDialogSnapshot snapshot) noexcept
    {
        fileDialogSnapshot = std::move(snapshot);
    }

    bool consumeMessageDialogResponse(Win32TestHooks::MessageDialogResponse &response) noexcept
    {
        if(!nextMessageDialogResponse)
        {
            return false;
        }

        response = *nextMessageDialogResponse;
        nextMessageDialogResponse.reset();
        return true;
    }

    void recordMessageDialogSnapshot(Win32TestHooks::MessageDialogSnapshot snapshot) noexcept
    {
        messageDialogSnapshot = std::move(snapshot);
    }

    bool consumePromptDialogResponse(Win32TestHooks::PromptDialogResponse &response) noexcept
    {
        if(!nextPromptDialogResponse)
        {
            return false;
        }

        response = *nextPromptDialogResponse;
        nextPromptDialogResponse.reset();
        return true;
    }

    void recordPromptDialogSnapshot(Win32TestHooks::PromptDialogSnapshot snapshot) noexcept
    {
        promptDialogSnapshot = std::move(snapshot);
    }
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop::Internal::TestHooks::Win32
{
    /// ------------------------------------------------------------
    /// Dialog controls and snapshots
    /// ------------------------------------------------------------

    void resetDialogState() noexcept
    {
        nextFileDialogResponse.reset();
        fileDialogSnapshot = {};
        nextMessageDialogResponse.reset();
        messageDialogSnapshot = {};
        nextPromptDialogResponse.reset();
        promptDialogSnapshot = {};
    }

    void completeNextFileDialog(FileDialogOperation operation, FileDialogResponse response) noexcept
    {
        nextFileDialogResponse.emplace(operation, std::move(response));
    }

    const FileDialogSnapshot &lastFileDialogSnapshot() noexcept
    {
        return fileDialogSnapshot;
    }

    void completeNextMessageDialog(MessageDialogResponse response) noexcept
    {
        nextMessageDialogResponse = response;
    }

    const MessageDialogSnapshot &lastMessageDialogSnapshot() noexcept
    {
        return messageDialogSnapshot;
    }

    void completeNextPromptDialog(PromptDialogResponse response) noexcept
    {
        nextPromptDialogResponse = response;
    }

    const PromptDialogSnapshot &lastPromptDialogSnapshot() noexcept
    {
        return promptDialogSnapshot;
    }

    GameWIP::Status::Types::Status testDialogApartment() noexcept
    {
        return Win32Backend::testDialogApartment();
    }

    GameWIP::Status::Types::Status testShellApartment() noexcept
    {
        return Win32Backend::testShellApartment();
    }

    GameWIP::Status::Types::Status createJumpLink(const Types::JumpLists::Task &task, void *&out) noexcept
    {
        return Win32Backend::createJumpLinkForTests(task, out);
    }

    /// ------------------------------------------------------------
    /// Progress dialog inspection
    /// ------------------------------------------------------------

    ProgressDialogNativeSnapshot inspectProgressDialog(const ProgressDialog &dialog) noexcept
    {
        return Win32Backend::inspectProgressDialog(Internal::ProgressDialogAccess::state(dialog));
    }

    GameWIP::Status::Types::Status requestProgressDialogCancel(ProgressDialog &dialog) noexcept
    {
        return Win32Backend::requestProgressDialogCancel(Internal::ProgressDialogAccess::state(dialog));
    }

    GameWIP::Status::Types::Status requestProgressDialogClose(ProgressDialog &dialog) noexcept
    {
        return Win32Backend::requestProgressDialogClose(Internal::ProgressDialogAccess::state(dialog));
    }

    GameWIP::Status::Types::Status destroyNativeProgressDialog(ProgressDialog &dialog) noexcept
    {
        return Win32Backend::destroyNativeProgressDialog(Internal::ProgressDialogAccess::state(dialog));
    }

    GameWIP::Status::Types::Status simulateProgressDialogDpiChange(ProgressDialog &dialog, NativePixelRect suggestedBounds, std::uint32_t dpi) noexcept
    {
        return Win32Backend::simulateProgressDialogDpiChange(Internal::ProgressDialogAccess::state(dialog), suggestedBounds, dpi);
    }

    std::size_t activeProgressDialogCount() noexcept
    {
        return Win32Backend::activeProgressDialogCount();
    }

    std::size_t deferredProgressDialogCount() noexcept
    {
        return Win32Backend::deferredProgressDialogCount();
    }

    std::size_t progressDialogClassReferenceCount() noexcept
    {
        return Win32Backend::progressDialogClassReferenceCount();
    }

    bool progressOwnerRestoreMessageRegistrationAttempted() noexcept
    {
        return Win32Backend::progressOwnerRestoreMessageRegistrationAttempted();
    }

    /// ------------------------------------------------------------
    /// Drag/drop controls and inspection
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status prepareDragDropSource(const Types::DragDrop::Description &description) noexcept
    {
        return Win32Backend::prepareDragDropSource(description);
    }

    GameWIP::Status::Types::Status testDragDropOleInitialization() noexcept
    {
        return Win32Backend::testDragDropOleInitialization();
    }

    GameWIP::Status::Types::Status testDragDropMaterialization() noexcept
    {
        return Win32Backend::testDragDropMaterialization();
    }

    Types::DragDrop::Result droppedDragDropSourceResult(Types::DragDrop::Effect performed, Types::DragDrop::Effect allowed) noexcept
    {
        return Win32Backend::testDroppedDragDropSourceResult(performed, allowed);
    }

    bool dragDropComContractsValid() noexcept
    {
        return Win32Backend::testDragDropComContracts();
    }

    Types::Events::PumpResult routeDragDropDuringPump(DragDropTarget &target, Types::DragDrop::Events::Payload data, bool terminal) noexcept
    {
        Internal::DragDropState *state = Internal::DragDropAccess::state(target);

        if(state == nullptr)
        {
            return Types::Events::PumpResult::failure(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen));
        }
        return Win32Backend::testRouteDragDropDuringPump(*state, std::move(data), terminal);
    }

    Types::DragDrop::RegionId matchDragDropRegion(
        const DragDropTarget &target,
        Types::LogicalPosition position,
        std::span<const Types::DataTransfer::FormatView> offered) noexcept
    {
        const Internal::DragDropState *state = Internal::DragDropAccess::state(target);
        return state == nullptr ? Types::DragDrop::RegionId{} : Win32Backend::testMatchDragDropRegion(*state, position, offered);
    }

    std::size_t activeDragDropTargetCount() noexcept
    {
        return Win32Backend::testActiveDragDropTargetCount();
    }

    std::size_t deferredDragDropTargetCount() noexcept
    {
        return Win32Backend::testDeferredDragDropTargetCount();
    }
} // namespace GameWIP::Desktop::Internal::TestHooks::Win32
