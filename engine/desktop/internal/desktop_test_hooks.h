/// @file desktop_test_hooks.h
/// @brief Source-tree-only deterministic hooks for Desktop validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "desktop/accessibility.h"
#include "desktop/child_surface.h"
#include "desktop/clipboard.h"
#include "desktop/cursor.h"
#include "desktop/description.h"
#include "desktop/display_info.h"
#include "desktop/drag_drop.h"
#include "desktop/events.h"
#include "desktop/internal/desktop_test_export.h"
#include "desktop/renderer_bridge.h"
#include "desktop/shell.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#ifndef DESKTOP_INTERNAL_TEST_HOOKS
#define DESKTOP_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Desktop::Internal::TestHooks
{
    /// ------------------------------------------------------------
    /// State snapshots for deterministic tests
    /// ------------------------------------------------------------

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
        // Display capabilities
        Types::Display::ColorSpace activeColorSpace = Types::Display::ColorSpace::Unknown;
        bool wideColorGamutSupported = false;
        bool hdrSupported = false;
        bool hdrEnabled = false;
        std::uint32_t bitsPerColorChannel = 0;

        // Luminance levels
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
        // Published client and display state
        Types::LogicalSize clientSize;
        Types::PixelSize framebufferSize;
        Types::ContentScale contentScale;
        Types::Dpi dpi;
        Types::Display::MonitorId monitor;
        Types::PresentationState presentation = Types::PresentationState::Normal;

        // Window interaction state
        bool visible = false;
        bool interactiveMoveResizeActive = false;
        bool occluded = false;
    };

    /// ------------------------------------------------------------
    /// Failure injection points
    /// ------------------------------------------------------------

    enum class FailurePoint
    {
        None,

        // Window creation and input regions
        Allocation,
        Dispatcher,
        NativeCreation,
        PartialOpen,
        TitleConversion,
        RegionCopy,
        IconConversion,

        // Cursor creation and binding
        Cursor,
        SystemCursorLoad,
        CursorBinding,
        CursorStateAllocation,

        // Display discovery and fullscreen transitions
        MonitorQuery,
        DisplayEnumeration,
        DisplayColorQuery,
        FullscreenPartial,
        DisplayRestoration,

        // Window close and event delivery
        Close,
        EventPump,

        // Clipboard operations
        ClipboardAllocation,
        ClipboardOwnerCreation,
        ClipboardAccess,
        ClipboardClear,
        ClipboardRead,
        ClipboardEnumeration,
        ClipboardClose,

        // Drag/drop target lifecycle
        DragDropOleInitialization,
        DragDropRegistration,
        DragDropRevocation,
        DragDropPreparation,
        DragDropMaterialization,

        // Dialog and progress lifecycle
        DialogConversion,
        DialogSuggestedDirectory,
        DialogNativeOperation,
        DialogResult,
        ProgressClassRegistration,
        ProgressClassRelease,
        ProgressOwnerBlocking,
        ProgressOwnerRestoreWake,
        ProgressMutation,

        // Native Window creation
        WindowStyleQuery,
        WindowUserDataInstallation,
        WindowCreationCallback,

        // Shell registration and lifetime
        ShellNativeOpen,
        ShellNativeApply,
        ShellNativeClose,
        ShellRegistryDelete
    };

#if DESKTOP_INTERNAL_TEST_HOOKS
    /// ------------------------------------------------------------
    /// Accessibility and failure hooks
    /// ------------------------------------------------------------

    /// @brief Submits through the real bounded action transport.
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::Accessibility::ActionResult submitAccessibilityAction(
        Window &window,
        Types::Accessibility::ActionRequest request) noexcept;
    /// @brief Removes one owner-thread pending notification without invoking native clients.
    [[nodiscard]] DESKTOP_TEST_EXPORT bool popAccessibilityNotification(Window &window, Types::Accessibility::Notification &out) noexcept;
    /// @brief Returns one AddRef'd IRawElementProviderSimple as void*, or nullptr. Caller must Release it.
    [[nodiscard]] DESKTOP_TEST_EXPORT void *accessibilityProvider(Window &window, Types::Accessibility::NodeId node) noexcept;
    DESKTOP_TEST_EXPORT void failNext(FailurePoint point) noexcept;
    DESKTOP_TEST_EXPORT void resetFailures() noexcept;
    /// ------------------------------------------------------------
    /// Window lifecycle and event hooks
    /// ------------------------------------------------------------

    [[nodiscard]] DESKTOP_TEST_EXPORT Types::Events::PumpResult pumpReentrantly() noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status openPortable(Window &window, std::span<Types::Event> storage) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status enqueue(Window &window, Types::Events::Payload data) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status enqueue(
        ChildSurface &surface,
        Types::ChildSurface::Events::Payload data) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status requestClose(Window &window, Types::Events::CloseRequestSource source) noexcept;
    /// @brief Applies renderer-facing test state and mirrors it when concurrent reads are enabled.
    DESKTOP_TEST_EXPORT void applyPresentationPublicationSnapshot(Window &window, const PresentationPublicationSnapshot &snapshot) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status destroyNativeWindow(Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status destroyNativeChildSurface(ChildSurface &surface) noexcept;

    /// ------------------------------------------------------------
    /// Drag/drop hooks
    /// ------------------------------------------------------------

    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::Effect negotiateDragDropEffect(
        Types::DragDrop::Effect source,
        Types::DragDrop::Effect target,
        Types::DragDrop::Effect preferred) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status enqueueDragDrop(
        DragDropTarget &target,
        Types::DragDrop::Events::Payload data,
        bool terminal = false) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT Types::DragDrop::SessionId nextDragDropSessionId(DragDropTarget &target, std::uint64_t nextValue) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT std::size_t dragDropRegionCount(const DragDropTarget &target) noexcept;

    /// ------------------------------------------------------------
    /// Renderer and display hooks
    /// ------------------------------------------------------------

    DESKTOP_TEST_EXPORT void enablePointerHitMaskBridge(Window &window) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool hasRendererIntegrationState(const Window &window) noexcept;
    /// @brief Returns the stable publication allocation identity, or nullptr while disabled.
    [[nodiscard]] DESKTOP_TEST_EXPORT const void *presentationPublicationStorage(const Window &window) noexcept;
    DESKTOP_TEST_EXPORT void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept;
    [[nodiscard]] DESKTOP_TEST_EXPORT GameWIP::Status::Types::Status simulateFullscreenMonitorRemoval(Window &window) noexcept;
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

    /// ------------------------------------------------------------
    /// Native failure controls and resource counts
    /// ------------------------------------------------------------

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

    /// ------------------------------------------------------------
    /// Shell event hooks
    /// ------------------------------------------------------------

    /// @brief Publishes one synthetic shell event through the queue's normal fixed-capacity path.
    DESKTOP_TEST_EXPORT void enqueueShellEvent(ShellEventQueue &queue, Types::Shell::Events::Payload payload) noexcept;
#endif
} // namespace GameWIP::Desktop::Internal::TestHooks

namespace GameWIP::Desktop::Internal
{
#if DESKTOP_INTERNAL_TEST_HOOKS
    [[nodiscard]] bool consumeFailure(TestHooks::FailurePoint point) noexcept;
    [[nodiscard]] bool consumeCursorNativeCreationFailure() noexcept;
    [[nodiscard]] bool consumeClipboardPublicationFailure(std::size_t itemIndex) noexcept;
    [[nodiscard]] bool consumeClipboardEnumerationFailure(std::size_t materializedFormats) noexcept;
    [[nodiscard]] bool consumeDragDropRevocationFailure() noexcept;
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

    constexpr void recordCustomCursorCreated() noexcept {}
    constexpr void recordCustomCursorDestroyed() noexcept {}
#endif
} // namespace GameWIP::Desktop::Internal
