/// @file test_hooks.h
/// @brief Source-tree-only test hooks for the Win32 backend.
/// @warning Not installed; validation code may include this header.

#pragma once

#include "desktop/dialogs.h"
#include "desktop/drag_drop.h"
#include "desktop/internal/desktop_test_export.h"
#include "desktop/shell_jump_lists.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifndef DESKTOP_INTERNAL_TEST_HOOKS
#define DESKTOP_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Desktop::Internal::TestHooks::Win32
{
#if DESKTOP_INTERNAL_TEST_HOOKS
    /// ------------------------------------------------------------
    /// Dialog inputs and native snapshots
    /// ------------------------------------------------------------

    enum class FileDialogOperation
    {
        OpenFile,
        OpenFiles,
        SaveFile,
        SelectFolder,
        SelectFolders
    };

    struct FileDialogResponse
    {
        bool accepted = false;
        std::vector<FileSystem::Types::Path> paths;
        std::optional<std::size_t> nativeFilterIndex;
    };

    struct FileDialogSnapshot
    {
        // Dialog title and operation
        FileDialogOperation operation = FileDialogOperation::OpenFile;
        std::wstring title;

        // Filter selection
        std::vector<std::wstring> filterNames;
        std::vector<std::wstring> filterPatterns;
        std::optional<std::size_t> preferredFilterIndex;

        // Initial location and file name
        FileSystem::Types::Path suggestedDirectory;
        std::wstring suggestedFileName;
        std::wstring suggestedExtension;
    };

    struct MessageDialogResponse
    {
        Types::Dialogs::Message::Button button = Types::Dialogs::Message::Button::None;
        bool dismissed = true;
    };

    struct MessageDialogSnapshot
    {
        // Visible message
        std::wstring title;
        std::wstring message;

        // Button and severity options
        Types::Dialogs::Message::Buttons buttons = Types::Dialogs::Message::Buttons::Ok;
        Types::Dialogs::Message::Button defaultButton = Types::Dialogs::Message::Button::None;
        Types::Dialogs::Severity severity = Types::Dialogs::Severity::None;
    };

    struct PromptDialogResponse
    {
        std::optional<std::size_t> buttonIndex;
        std::optional<std::size_t> optionIndex;
        bool checkBoxChecked = false;
        bool dismissed = true;
    };

    struct PromptDialogSnapshot
    {
        // Prompt content
        std::wstring title;
        std::wstring heading;
        std::wstring message;
        std::wstring details;
        std::wstring supplementalText;
        std::wstring checkBoxLabel;

        // Native button and option mappings
        std::vector<int> nativeButtonIds;
        std::vector<int> nativeOptionIds;
        std::vector<std::wstring> buttonText;
        std::vector<std::wstring> optionText;

        // Initial selection and behavior
        int nativeDefaultButton = 0;
        int nativeDefaultOption = 0;
        bool commandLinks = false;
        bool checkBoxInitiallyChecked = false;
        Types::Dialogs::Severity severity = Types::Dialogs::Severity::None;
    };

    /// ------------------------------------------------------------
    /// Progress dialog native state
    /// ------------------------------------------------------------

    struct NativePixelRect
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        friend constexpr bool operator==(NativePixelRect, NativePixelRect) noexcept = default;
    };

    struct ProgressDialogNativeSnapshot
    {
        // Geometry
        NativePixelRect windowBounds;
        NativePixelRect progressBounds;

        // Display text
        std::wstring title;
        std::wstring heading;
        std::wstring message;

        // Progress state
        int rangeMinimum = 0;
        int rangeMaximum = 0;
        int position = 0;
        bool marquee = false;

        // Native controls
        bool nativeWindow = false;
        bool cancelControl = false;

        // Ownership and teardown
        bool registered = false;
        bool classReferenceHeld = false;
        bool blockingOwner = false;
        bool nativeDestroyedPendingFinalize = false;
    };

    /// ------------------------------------------------------------
    /// Dialog and shell inspection
    /// ------------------------------------------------------------

    DESKTOP_TEST_EXPORT void resetDialogState() noexcept;
    DESKTOP_TEST_EXPORT void completeNextFileDialog(FileDialogOperation operation, FileDialogResponse response) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const FileDialogSnapshot &lastFileDialogSnapshot() noexcept;
    DESKTOP_TEST_EXPORT void completeNextMessageDialog(MessageDialogResponse response) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const MessageDialogSnapshot &lastMessageDialogSnapshot() noexcept;
    DESKTOP_TEST_EXPORT void completeNextPromptDialog(PromptDialogResponse response) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const PromptDialogSnapshot &lastPromptDialogSnapshot() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status testDialogApartment() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status testShellApartment() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status createJumpLink(const Types::JumpLists::Task &task, void *&out) noexcept;

    /// ------------------------------------------------------------
    /// Progress dialog inspection and control
    /// ------------------------------------------------------------

    [[nodiscard]] DESKTOP_TEST_EXPORT ProgressDialogNativeSnapshot inspectProgressDialog(const ProgressDialog &dialog) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status requestProgressDialogCancel(ProgressDialog &dialog) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status requestProgressDialogClose(ProgressDialog &dialog) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status destroyNativeProgressDialog(ProgressDialog &dialog) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status simulateProgressDialogDpiChange(
        ProgressDialog &dialog,
        NativePixelRect suggestedBounds,
        std::uint32_t dpi) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t activeProgressDialogCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t deferredProgressDialogCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t progressDialogClassReferenceCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool progressOwnerRestoreMessageRegistrationAttempted() noexcept;

    /// ------------------------------------------------------------
    /// Drag/drop native contracts
    /// ------------------------------------------------------------

    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status prepareDragDropSource(const Types::DragDrop::Description &description) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status testDragDropOleInitialization() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status testDragDropMaterialization() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::Result droppedDragDropSourceResult(
        Types::DragDrop::Effect performed,
        Types::DragDrop::Effect allowed) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool dragDropComContractsValid() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::Events::PumpResult routeDragDropDuringPump(
        DragDropTarget &target,
        Types::DragDrop::Events::Payload data,
        bool terminal = false) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::RegionId matchDragDropRegion(
        const DragDropTarget &target,
        Types::LogicalPosition position,
        std::span<const Types::DataTransfer::FormatView> offered) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t activeDragDropTargetCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t deferredDragDropTargetCount() noexcept;
#endif
} // namespace GameWIP::Desktop::Internal::TestHooks::Win32

namespace GameWIP::Desktop::Internal
{
#if DESKTOP_INTERNAL_TEST_HOOKS
    /// ------------------------------------------------------------
    /// Dialog fixture storage
    /// ------------------------------------------------------------

    [[nodiscard]] bool consumeFileDialogResponse(
        TestHooks::Win32::FileDialogOperation operation,
        TestHooks::Win32::FileDialogResponse &response) noexcept;
    void recordFileDialogSnapshot(TestHooks::Win32::FileDialogSnapshot snapshot) noexcept;
    [[nodiscard]] bool consumeMessageDialogResponse(TestHooks::Win32::MessageDialogResponse &response) noexcept;
    void recordMessageDialogSnapshot(TestHooks::Win32::MessageDialogSnapshot snapshot) noexcept;
    [[nodiscard]] bool consumePromptDialogResponse(TestHooks::Win32::PromptDialogResponse &response) noexcept;
    void recordPromptDialogSnapshot(TestHooks::Win32::PromptDialogSnapshot snapshot) noexcept;
#endif

    /// ------------------------------------------------------------
    /// Native state types
    /// ------------------------------------------------------------

    struct DragDropState;
    struct ProgressDialogState;
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop::Internal::TestHooks::Platform
{
#if DESKTOP_INTERNAL_TEST_HOOKS
    /// ------------------------------------------------------------
    /// Dialog and shell hooks
    /// ------------------------------------------------------------

    [[nodiscard]] IO::Types::Status testDialogApartment() noexcept;
    [[nodiscard]] IO::Types::Status testShellApartment() noexcept;
    [[nodiscard]] IO::Types::Status createJumpLinkForTests(const Types::JumpLists::Task &task, void *&out) noexcept;

    /// ------------------------------------------------------------
    /// Progress dialog hooks
    /// ------------------------------------------------------------

    [[nodiscard]] Win32::ProgressDialogNativeSnapshot inspectProgressDialog(const ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status requestProgressDialogCancel(ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status requestProgressDialogClose(ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status destroyNativeProgressDialog(ProgressDialogState *state) noexcept;
    [[nodiscard]] IO::Types::Status simulateProgressDialogDpiChange(
        ProgressDialogState *state,
        Win32::NativePixelRect suggestedBounds,
        std::uint32_t dpi) noexcept;
    [[nodiscard]] std::size_t activeProgressDialogCount() noexcept;
    [[nodiscard]] std::size_t deferredProgressDialogCount() noexcept;
    [[nodiscard]] std::size_t progressDialogClassReferenceCount() noexcept;
    [[nodiscard]] bool progressOwnerRestoreMessageRegistrationAttempted() noexcept;

    /// ------------------------------------------------------------
    /// Drag/drop hooks
    /// ------------------------------------------------------------

    [[nodiscard]] IO::Types::Status prepareDragDropSource(const Types::DragDrop::Description &description) noexcept;
    [[nodiscard]] IO::Types::Status testDragDropOleInitialization() noexcept;
    [[nodiscard]] IO::Types::Status testDragDropMaterialization() noexcept;
    [[nodiscard]] Types::DragDrop::Result testDroppedDragDropSourceResult(
        Types::DragDrop::Effect performed,
        Types::DragDrop::Effect allowed) noexcept;
    [[nodiscard]] bool testDragDropComContracts() noexcept;
    [[nodiscard]] Types::Events::PumpResult testRouteDragDropDuringPump(
        DragDropState &state,
        Types::DragDrop::Events::Payload data,
        bool terminal) noexcept;
    [[nodiscard]] Types::DragDrop::RegionId testMatchDragDropRegion(
        const DragDropState &state,
        Types::LogicalPosition position,
        std::span<const Types::DataTransfer::FormatView> offered) noexcept;
    [[nodiscard]] std::size_t testActiveDragDropTargetCount() noexcept;
    [[nodiscard]] std::size_t testDeferredDragDropTargetCount() noexcept;
#endif
} // namespace GameWIP::Desktop::Internal::TestHooks::Platform
