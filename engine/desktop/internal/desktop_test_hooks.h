/// @file desktop_test_hooks.h
/// @brief Source-tree-only deterministic hooks for Window validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "desktop/child_surface.h"
#include "desktop/internal/desktop_test_export.h"
#include "desktop/clipboard.h"
#include "desktop/cursor.h"
#include "desktop/description.h"
#include "desktop/dialogs.h"
#include "desktop/display_info.h"
#include "desktop/drag_drop.h"
#include "desktop/events.h"
#include "desktop/renderer_bridge.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

#ifndef DESKTOP_INTERNAL_TEST_HOOKS
#define DESKTOP_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Desktop::TestHooks
{
    struct DpiTransitionResult
    {
        Types::LogicalSize logicalSize;
        Types::PixelSize framebufferSize;
    };

    struct ChildSurfaceDpiTransitionResult
    {
        Types::LogicalRect logicalRect;
        Types::PixelSize pixelSize;
    };

    struct DisplayColorSnapshot
    {
        Types::Display::ColorSpace activeColorSpace = Types::Display::ColorSpace::Unknown;
        bool wideColorGamutSupported = false;
        bool hdrSupported = false;
        bool hdrEnabled = false;
        std::uint32_t bitsPerColorChannel = 0;
        float minimumLuminanceNits = 0.0F;
        float maximumLuminanceNits = 0.0F;
        float maximumFullFrameLuminanceNits = 0.0F;
        std::uint32_t sdrWhiteLevelMilli80Nits = 0;
    };

    struct CustomCursorNativeSnapshot
    {
        Types::Cursor::PixelPosition hotspot;
        std::array<std::byte, 4> firstBgraPixel{};
        bool valid = false;
    };

    /// @brief Complete deterministic input for renderer-facing presentation publication tests.
    struct PresentationPublicationSnapshot
    {
        Types::LogicalSize clientSize;
        Types::PixelSize framebufferSize;
        Types::ContentScale contentScale;
        Types::Dpi dpi;
        Types::Display::MonitorId monitor;
        Types::PresentationState presentation = Types::PresentationState::Normal;
        bool visible = false;
        bool interactiveMoveResizeActive = false;
        bool occluded = false;
    };

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
        FileDialogOperation operation = FileDialogOperation::OpenFile;
        std::wstring title;
        std::vector<std::wstring> filterNames;
        std::vector<std::wstring> filterPatterns;
        std::optional<std::size_t> preferredFilterIndex;
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
        std::wstring title;
        std::wstring message;
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
        std::wstring title;
        std::wstring heading;
        std::wstring message;
        std::wstring details;
        std::wstring supplementalText;
        std::wstring checkBoxLabel;
        std::vector<int> nativeButtonIds;
        std::vector<int> nativeOptionIds;
        std::vector<std::wstring> buttonText;
        std::vector<std::wstring> optionText;
        int nativeDefaultButton = 0;
        int nativeDefaultOption = 0;
        bool commandLinks = false;
        bool checkBoxInitiallyChecked = false;
        Types::Dialogs::Severity severity = Types::Dialogs::Severity::None;
    };

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
        NativePixelRect windowBounds;
        NativePixelRect progressBounds;
        std::wstring title;
        std::wstring heading;
        std::wstring message;
        int rangeMinimum = 0;
        int rangeMaximum = 0;
        int position = 0;
        bool nativeWindow = false;
        bool cancelControl = false;
        bool marquee = false;
        bool registered = false;
        bool classReferenceHeld = false;
        bool blockingOwner = false;
        bool nativeDestroyedPendingFinalize = false;
    };

    enum class FailurePoint
    {
        None,
        Allocation,
        Dispatcher,
        NativeCreation,
        PartialOpen,
        TitleConversion,
        RegionCopy,
        IconConversion,
        Cursor,
        SystemCursorLoad,
        CursorBinding,
        CursorStateAllocation,
        MonitorQuery,
        DisplayEnumeration,
        DisplayColorQuery,
        FullscreenPartial,
        DisplayRestoration,
        Close,
        EventPump,
        ClipboardAllocation,
        ClipboardTextConversion,
        ClipboardPathConversion,
        ClipboardImagePreparation,
        ClipboardOwnerCreation,
        ClipboardAccess,
        ClipboardClear,
        ClipboardRead,
        ClipboardEnumeration,
        ClipboardRegistration,
        ClipboardClose,
        DragDropOleInitialization,
        DragDropRegistration,
        DragDropRevocation,
        DragDropPreparation,
        DragDropMaterialization,
        DialogConversion,
        DialogSuggestedDirectory,
        DialogNativeOperation,
        DialogResult,
        ProgressClassRegistration,
        ProgressClassRelease,
        ProgressOwnerBlocking,
        ProgressOwnerRestoreWake,
        ProgressMutation,
        WindowStyleQuery,
        WindowUserDataInstallation
    };

#if DESKTOP_INTERNAL_TEST_HOOKS
    DESKTOP_TEST_EXPORT void failNext(FailurePoint point) noexcept;
    DESKTOP_TEST_EXPORT void resetFailures() noexcept;
    DESKTOP_TEST_EXPORT void completeNextFileDialog(FileDialogOperation operation, FileDialogResponse response) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const FileDialogSnapshot &lastFileDialogSnapshot() noexcept;
    DESKTOP_TEST_EXPORT void completeNextMessageDialog(MessageDialogResponse response) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const MessageDialogSnapshot &lastMessageDialogSnapshot() noexcept;
    DESKTOP_TEST_EXPORT void completeNextPromptDialog(PromptDialogResponse response) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const PromptDialogSnapshot &lastPromptDialogSnapshot() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status testDialogApartment() noexcept;
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
    /// @brief Returns whether progress-owner message registration has been attempted in this process.
    [[nodiscard]] DESKTOP_TEST_EXPORT bool progressOwnerRestoreMessageRegistrationAttempted() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::Events::PumpResult pumpReentrantly() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status openPortable(Window &window, std::span<Types::Event> storage) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status enqueue(Window &window, Types::Events::Payload data) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status requestClose(Window &window, Types::Events::CloseRequestSource source) noexcept;
    /// @brief Applies renderer-facing test state and mirrors it when concurrent reads are enabled.
    DESKTOP_TEST_EXPORT void applyPresentationPublicationSnapshot(Window &window, const PresentationPublicationSnapshot &snapshot) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status destroyNativeWindow(Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status destroyNativeChildSurface(ChildSurface &surface) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::Effect negotiateDragDropEffect(
        Types::DragDrop::Effect source,
        Types::DragDrop::Effect target,
        Types::DragDrop::Effect preferred) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status enqueueDragDrop(
        DragDropTarget &target,
        Types::DragDrop::Events::Payload data,
        bool terminal = false) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::SessionId nextDragDropSessionId(DragDropTarget &target, std::uint64_t nextValue) noexcept;
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
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t dragDropRegionCount(const DragDropTarget &target) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t activeDragDropTargetCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t deferredDragDropTargetCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::RegionId matchDragDropRegion(
        const DragDropTarget &target,
        Types::LogicalPosition position,
        std::span<const Types::DataTransfer::FormatView> offered) noexcept;
    DESKTOP_TEST_EXPORT void enablePointerHitMaskBridge(Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool hasRendererIntegrationState(const Window &window) noexcept;
    /// @brief Returns the stable publication allocation identity, or nullptr while disabled.
    [[nodiscard]] DESKTOP_TEST_EXPORT const void *presentationPublicationStorage(const Window &window) noexcept;
    DESKTOP_TEST_EXPORT void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT IO::Types::Status simulateFullscreenMonitorRemoval(Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::uint64_t pointerHitMaskGeneration(const Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t pointerHitMaskWordCount(const Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::Renderer::PointerHitMaskWord pointerHitMaskWord(const Window &window, std::size_t index) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT const void *pointerHitMaskStorage(const Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::uint32_t refreshRateMillihertz(std::uint32_t numerator, std::uint32_t denominator) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::Display::ColorInfo makeDisplayColorInfo(
        Types::Display::MonitorId monitor,
        const DisplayColorSnapshot &snapshot) noexcept;
    DESKTOP_TEST_EXPORT void simulateDisplayColorConfigurationChange() noexcept;
    DESKTOP_TEST_EXPORT void makeNextDisplayColorMetadataUnavailable() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool exactNativeDisplayModeMatches(
        const Types::Display::Mode &requested,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t frequencyHertz,
        std::uint16_t bitsPerPixel,
        bool interlaced) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT DpiTransitionResult calculateDpiTransition(
        Types::LogicalSize logicalSize,
        Types::PixelSize framebufferSize,
        std::uint32_t newDpi,
        Types::DpiResizePolicy policy) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT ChildSurfaceDpiTransitionResult
    calculateChildSurfaceDpiTransition(Types::LogicalRect logicalRect, std::uint32_t newDpi) noexcept;
    DESKTOP_TEST_EXPORT void failCursorNativeCreationAfter(std::size_t successfulVariants) noexcept;
    /// @brief Fails publication of the zero-based requested Clipboard item index once.
    DESKTOP_TEST_EXPORT void failClipboardPublicationAt(std::size_t itemIndex) noexcept;
    /// @brief Fails Clipboard enumeration after the requested number of materialized formats once.
    DESKTOP_TEST_EXPORT void failClipboardEnumerationAfter(std::size_t materializedFormats) noexcept;
    /// @brief Fails the requested number of consecutive native DragDrop revocations.
    DESKTOP_TEST_EXPORT void failDragDropRevocations(std::size_t attempts) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t customCursorVariantCount(const Cursor &cursor) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::uint32_t customCursorBindingDpi(const Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t createdCustomCursorCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t destroyedCustomCursorCount() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT CustomCursorNativeSnapshot inspectCustomCursorVariant(const Cursor &cursor, std::size_t index) noexcept;
#endif
} // namespace GameWIP::Desktop::TestHooks

namespace GameWIP::Desktop::Detail
{
#if DESKTOP_INTERNAL_TEST_HOOKS
    [[nodiscard]] bool consumeFailure(TestHooks::FailurePoint point) noexcept;
    [[nodiscard]] bool consumeCursorNativeCreationFailure() noexcept;
    [[nodiscard]] bool consumeClipboardPublicationFailure(std::size_t itemIndex) noexcept;
    [[nodiscard]] bool consumeClipboardEnumerationFailure(std::size_t materializedFormats) noexcept;
    [[nodiscard]] bool consumeDragDropRevocationFailure() noexcept;
    [[nodiscard]] bool consumeFileDialogResponse(TestHooks::FileDialogOperation operation, TestHooks::FileDialogResponse &response) noexcept;
    void recordFileDialogSnapshot(TestHooks::FileDialogSnapshot snapshot) noexcept;
    [[nodiscard]] bool consumeMessageDialogResponse(TestHooks::MessageDialogResponse &response) noexcept;
    void recordMessageDialogSnapshot(TestHooks::MessageDialogSnapshot snapshot) noexcept;
    [[nodiscard]] bool consumePromptDialogResponse(TestHooks::PromptDialogResponse &response) noexcept;
    void recordPromptDialogSnapshot(TestHooks::PromptDialogSnapshot snapshot) noexcept;
    void recordCustomCursorCreated() noexcept;
    void recordCustomCursorDestroyed() noexcept;
#else
    [[nodiscard]] constexpr bool consumeFailure(TestHooks::FailurePoint) noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool consumeCursorNativeCreationFailure() noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool consumeClipboardPublicationFailure(std::size_t) noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool consumeClipboardEnumerationFailure(std::size_t) noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool consumeDragDropRevocationFailure() noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool consumeFileDialogResponse(TestHooks::FileDialogOperation, TestHooks::FileDialogResponse &) noexcept
    {
        return false;
    }
    constexpr void recordFileDialogSnapshot(TestHooks::FileDialogSnapshot) noexcept {}
    [[nodiscard]] constexpr bool consumeMessageDialogResponse(TestHooks::MessageDialogResponse &) noexcept
    {
        return false;
    }
    constexpr void recordMessageDialogSnapshot(TestHooks::MessageDialogSnapshot) noexcept {}
    [[nodiscard]] constexpr bool consumePromptDialogResponse(TestHooks::PromptDialogResponse &) noexcept
    {
        return false;
    }
    constexpr void recordPromptDialogSnapshot(TestHooks::PromptDialogSnapshot) noexcept {}
    constexpr void recordCustomCursorCreated() noexcept {}
    constexpr void recordCustomCursorDestroyed() noexcept {}
#endif
} // namespace GameWIP::Desktop::Detail
