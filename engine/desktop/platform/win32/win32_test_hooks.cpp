/// @file win32_test_hooks.cpp
/// @brief Source-tree-only test adapter for Win32-specific Desktop hooks.

#include "desktop/platform/win32/internal/test_hooks.h"

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/progress_dialog_state.h"
#include "desktop/internal/window_state.h"

#include <utility>

namespace
{
    namespace Win32TestHooks = GameWIP::Desktop::Internal::TestHooks::Win32;
    namespace PlatformTestHooks = GameWIP::Desktop::Internal::TestHooks::Platform;

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

    IO::Types::Status testDialogApartment() noexcept
    {
        return PlatformTestHooks::testDialogApartment();
    }

    IO::Types::Status testShellApartment() noexcept
    {
        return PlatformTestHooks::testShellApartment();
    }

    IO::Types::Status createJumpLink(const Types::JumpLists::Task &task, void *&out) noexcept
    {
        return PlatformTestHooks::createJumpLinkForTests(task, out);
    }

    /// ------------------------------------------------------------
    /// Progress dialog inspection
    /// ------------------------------------------------------------

    ProgressDialogNativeSnapshot inspectProgressDialog(const ProgressDialog &dialog) noexcept
    {
        return PlatformTestHooks::inspectProgressDialog(Internal::ProgressDialogAccess::state(dialog));
    }

    IO::Types::Status requestProgressDialogCancel(ProgressDialog &dialog) noexcept
    {
        return PlatformTestHooks::requestProgressDialogCancel(Internal::ProgressDialogAccess::state(dialog));
    }

    IO::Types::Status requestProgressDialogClose(ProgressDialog &dialog) noexcept
    {
        return PlatformTestHooks::requestProgressDialogClose(Internal::ProgressDialogAccess::state(dialog));
    }

    IO::Types::Status destroyNativeProgressDialog(ProgressDialog &dialog) noexcept
    {
        return PlatformTestHooks::destroyNativeProgressDialog(Internal::ProgressDialogAccess::state(dialog));
    }

    IO::Types::Status simulateProgressDialogDpiChange(ProgressDialog &dialog, NativePixelRect suggestedBounds, std::uint32_t dpi) noexcept
    {
        return PlatformTestHooks::simulateProgressDialogDpiChange(Internal::ProgressDialogAccess::state(dialog), suggestedBounds, dpi);
    }

    std::size_t activeProgressDialogCount() noexcept
    {
        return PlatformTestHooks::activeProgressDialogCount();
    }

    std::size_t deferredProgressDialogCount() noexcept
    {
        return PlatformTestHooks::deferredProgressDialogCount();
    }

    std::size_t progressDialogClassReferenceCount() noexcept
    {
        return PlatformTestHooks::progressDialogClassReferenceCount();
    }

    bool progressOwnerRestoreMessageRegistrationAttempted() noexcept
    {
        return PlatformTestHooks::progressOwnerRestoreMessageRegistrationAttempted();
    }

    /// ------------------------------------------------------------
    /// Drag/drop controls and inspection
    /// ------------------------------------------------------------

    IO::Types::Status prepareDragDropSource(const Types::DragDrop::Description &description) noexcept
    {
        return PlatformTestHooks::prepareDragDropSource(description);
    }

    IO::Types::Status testDragDropOleInitialization() noexcept
    {
        return PlatformTestHooks::testDragDropOleInitialization();
    }

    IO::Types::Status testDragDropMaterialization() noexcept
    {
        return PlatformTestHooks::testDragDropMaterialization();
    }

    Types::DragDrop::Result droppedDragDropSourceResult(Types::DragDrop::Effect performed, Types::DragDrop::Effect allowed) noexcept
    {
        return PlatformTestHooks::testDroppedDragDropSourceResult(performed, allowed);
    }

    bool dragDropComContractsValid() noexcept
    {
        return PlatformTestHooks::testDragDropComContracts();
    }

    Types::Events::PumpResult routeDragDropDuringPump(DragDropTarget &target, Types::DragDrop::Events::Payload data, bool terminal) noexcept
    {
        Types::Events::PumpResult result;
        Internal::DragDropState *state = Internal::DragDropAccess::state(target);

        if(state == nullptr)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            return result;
        }
        return PlatformTestHooks::testRouteDragDropDuringPump(*state, std::move(data), terminal);
    }

    Types::DragDrop::RegionId matchDragDropRegion(
        const DragDropTarget &target,
        Types::LogicalPosition position,
        std::span<const Types::DataTransfer::FormatView> offered) noexcept
    {
        const Internal::DragDropState *state = Internal::DragDropAccess::state(target);
        return state == nullptr ? Types::DragDrop::RegionId{} : PlatformTestHooks::testMatchDragDropRegion(*state, position, offered);
    }

    std::size_t activeDragDropTargetCount() noexcept
    {
        return PlatformTestHooks::testActiveDragDropTargetCount();
    }

    std::size_t deferredDragDropTargetCount() noexcept
    {
        return PlatformTestHooks::testDeferredDragDropTargetCount();
    }
} // namespace GameWIP::Desktop::Internal::TestHooks::Win32
