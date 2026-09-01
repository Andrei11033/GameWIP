/// @file desktop_test_hooks.cpp
/// @brief Source-tree-only deterministic Window hooks.

#include "desktop/internal/desktop_test_hooks.h"

#if DESKTOP_INTERNAL_TEST_HOOKS
#include "desktop/internal/cursor_platform.h"
#include "desktop/internal/cursor_state.h"
#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/drag_drop_platform.h"
#include "desktop/internal/window_platform.h"
#include "desktop/internal/window_state.h"

#include <atomic>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace
{
    thread_local GameWIP::Desktop::TestHooks::FailurePoint armedFailure = GameWIP::Desktop::TestHooks::FailurePoint::None;
    thread_local std::size_t cursorNativeCreationFailureCountdown = std::numeric_limits<std::size_t>::max();
    thread_local std::size_t clipboardPublicationFailureIndex = std::numeric_limits<std::size_t>::max();
    thread_local std::size_t clipboardEnumerationFailureCount = std::numeric_limits<std::size_t>::max();
    thread_local std::size_t dragDropRevocationFailures = 0;
    std::atomic_size_t customCursorsCreated = 0;
    std::atomic_size_t customCursorsDestroyed = 0;
} // namespace

namespace GameWIP::Desktop::Detail
{
    // ------------------------------------------------------------
    // Injected failures and counters
    // ------------------------------------------------------------
    bool consumeFailure(TestHooks::FailurePoint point) noexcept
    {
        if (armedFailure != point)
            return false;
        armedFailure = TestHooks::FailurePoint::None;
        return true;
    }

    bool consumeCursorNativeCreationFailure() noexcept
    {
        if (cursorNativeCreationFailureCountdown == std::numeric_limits<std::size_t>::max())
            return false;
        if (cursorNativeCreationFailureCountdown != 0)
        {
            --cursorNativeCreationFailureCountdown;
            return false;
        }
        cursorNativeCreationFailureCountdown = std::numeric_limits<std::size_t>::max();
        return true;
    }

    bool consumeClipboardPublicationFailure(std::size_t itemIndex) noexcept
    {
        if (clipboardPublicationFailureIndex != itemIndex)
            return false;
        clipboardPublicationFailureIndex = std::numeric_limits<std::size_t>::max();
        return true;
    }

    bool consumeClipboardEnumerationFailure(std::size_t materializedFormats) noexcept
    {
        if (clipboardEnumerationFailureCount != materializedFormats)
            return false;
        clipboardEnumerationFailureCount = std::numeric_limits<std::size_t>::max();
        return true;
    }

    bool consumeDragDropRevocationFailure() noexcept
    {
        if (dragDropRevocationFailures == 0)
            return false;
        --dragDropRevocationFailures;
        return true;
    }

    void recordCustomCursorCreated() noexcept
    {
        customCursorsCreated.fetch_add(1, std::memory_order_relaxed);
    }

    void recordCustomCursorDestroyed() noexcept
    {
        customCursorsDestroyed.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop::TestHooks
{
    // ------------------------------------------------------------
    // Failure controls
    // ------------------------------------------------------------
    void failNext(FailurePoint point) noexcept
    {
        armedFailure = point;
    }

    void resetFailures() noexcept
    {
        armedFailure = FailurePoint::None;
        cursorNativeCreationFailureCountdown = std::numeric_limits<std::size_t>::max();
        clipboardPublicationFailureIndex = std::numeric_limits<std::size_t>::max();
        clipboardEnumerationFailureCount = std::numeric_limits<std::size_t>::max();
        dragDropRevocationFailures = 0;
    }

    // ------------------------------------------------------------
    // DragDrop policy and lifecycle controls
    // ------------------------------------------------------------

    Types::DragDrop::Effect negotiateDragDropEffect(
        Types::DragDrop::Effect source,
        Types::DragDrop::Effect target,
        Types::DragDrop::Effect preferred) noexcept
    {
        return Detail::negotiateDragDropEffect(source, target, preferred);
    }

    IO::Types::Status enqueueDragDrop(DragDropTarget &target, Types::DragDrop::Events::Payload data, bool terminal) noexcept
    {
        Detail::DragDropState *state = Detail::DragDropAccess::state(target);
        if (!state)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        return Detail::enqueueDragDropEvent(*state, std::move(data), terminal) ? IO::successStatus()
                                                                               : IO::makeStatus(IO::Types::ErrorCode::StorageFull);
    }

    Types::DragDrop::SessionId nextDragDropSessionId(DragDropTarget &target, std::uint64_t nextValue) noexcept
    {
        Detail::DragDropState *state = Detail::DragDropAccess::state(target);
        if (state == nullptr)
            return {};
        state->nextSessionId = nextValue;
        return Detail::allocateDragDropSessionId(*state);
    }

    IO::Types::Status prepareDragDropSource(const Types::DragDrop::Description &description) noexcept
    {
        return Detail::Platform::prepareDragDropSource(description);
    }

    IO::Types::Status testDragDropOleInitialization() noexcept
    {
        return Detail::Platform::testDragDropOleInitialization();
    }

    IO::Types::Status testDragDropMaterialization() noexcept
    {
        return Detail::Platform::testDragDropMaterialization();
    }

    bool dragDropComContractsValid() noexcept
    {
        return Detail::Platform::testDragDropComContracts();
    }

    void failDragDropRevocations(std::size_t attempts) noexcept
    {
        dragDropRevocationFailures = attempts;
    }

    Types::Events::PumpResult routeDragDropDuringPump(DragDropTarget &target, Types::DragDrop::Events::Payload data, bool terminal) noexcept
    {
        Types::Events::PumpResult result;
        Detail::DragDropState *state = Detail::DragDropAccess::state(target);
        if (state == nullptr)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            return result;
        }
        return Detail::Platform::testRouteDragDropDuringPump(*state, std::move(data), terminal);
    }

    std::size_t dragDropRegionCount(const DragDropTarget &target) noexcept
    {
        const Detail::DragDropState *state = Detail::DragDropAccess::state(target);
        return state == nullptr ? 0 : state->regions.size();
    }

    std::size_t activeDragDropTargetCount() noexcept
    {
        return Detail::Platform::testActiveDragDropTargetCount();
    }

    std::size_t deferredDragDropTargetCount() noexcept
    {
        return Detail::Platform::testDeferredDragDropTargetCount();
    }

    Types::DragDrop::RegionId matchDragDropRegion(
        const DragDropTarget &target,
        Types::LogicalPosition position,
        std::span<const Types::DataTransfer::FormatView> offered) noexcept
    {
        const Detail::DragDropState *state = Detail::DragDropAccess::state(target);
        return state == nullptr ? Types::DragDrop::RegionId{} : Detail::Platform::testMatchDragDropRegion(*state, position, offered);
    }

    // ------------------------------------------------------------
    // Cursor and Clipboard controls
    // ------------------------------------------------------------

    void failCursorNativeCreationAfter(std::size_t successfulVariants) noexcept
    {
        cursorNativeCreationFailureCountdown = successfulVariants;
    }

    void failClipboardPublicationAt(std::size_t itemIndex) noexcept
    {
        clipboardPublicationFailureIndex = itemIndex;
    }

    void failClipboardEnumerationAfter(std::size_t materializedFormats) noexcept
    {
        clipboardEnumerationFailureCount = materializedFormats;
    }

    std::size_t customCursorVariantCount(const Cursor &cursor) noexcept
    {
        const auto &state = Detail::CursorAccess::state(cursor);
        return state ? state->variants.size() : 0;
    }

    std::uint32_t customCursorBindingDpi(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !state->platform || !Detail::Platform::hasCustomCursor(*state))
            return 0;
        return Detail::Platform::customCursorBindingDpi(*state);
    }

    std::size_t createdCustomCursorCount() noexcept
    {
        return customCursorsCreated.load(std::memory_order_relaxed);
    }

    std::size_t destroyedCustomCursorCount() noexcept
    {
        return customCursorsDestroyed.load(std::memory_order_relaxed);
    }

    CustomCursorNativeSnapshot inspectCustomCursorVariant(const Cursor &cursor, std::size_t index) noexcept
    {
        const auto &state = Detail::CursorAccess::state(cursor);
        if (!state || index >= state->variants.size())
            return {};
        const Detail::Platform::NativeCursorSnapshot snapshot = Detail::Platform::inspectNativeCursor(state->variants[index]);
        return {{snapshot.hotspotX, snapshot.hotspotY}, snapshot.firstBgraPixel, snapshot.valid};
    }

    // ------------------------------------------------------------
    // Renderer bridge controls
    // ------------------------------------------------------------
    void enablePointerHitMaskBridge(Window &window) noexcept
    {
        try
        {
            if (Detail::WindowAccess::state(window) != nullptr)
                Detail::WindowAccess::ensureRendererIntegration(window)->pointerHitMaskBackendSupportedForTesting = true;
        }
        catch (const std::bad_alloc &)
        {
            return;
        }
    }

    bool hasRendererIntegrationState(const Window &window) noexcept
    {
        return Detail::WindowAccess::rendererIntegration(window) != nullptr;
    }

    const void *presentationPublicationStorage(const Window &window) noexcept
    {
        return Detail::WindowAccess::presentationPublication(window);
    }

    void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept
    {
        Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        if (renderer != nullptr)
        {
            renderer->pointerHitMaskGeneration = generation;
            renderer->pointerHitMaskGenerationExhausted = false;
            renderer->pointerHitMaskTargetGeneration = 0;
        }
    }

    bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state == nullptr || Detail::pointerHitMaskAccepts(*state, position);
    }

    // ------------------------------------------------------------
    // Portable lifecycle and events
    // ------------------------------------------------------------
    IO::Types::Status openPortable(Window &window, std::span<Types::Event> storage) noexcept
    {
        if (Detail::WindowAccess::state(window) != nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        if (storage.empty())
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        try
        {
            auto state = std::make_unique<Detail::WindowState>();
            state->ownerThread = std::this_thread::get_id();
            state->id = {1};
            state->eventStorage = storage;
            state->eventStorageKind = Types::Events::StorageKind::External;
            Detail::WindowAccess::bindRendererIntegration(window, *state);
            if (Detail::WindowAccess::presentationPublication(window) != nullptr)
            {
                Detail::WindowAccess::bindPresentationPublication(window, *state);
                Detail::publishCachedPresentationState(*state);
            }
            Detail::WindowAccess::stateOwner(window) = std::move(state);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    IO::Types::Status enqueue(Window &window, Types::Events::Payload data) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        static_cast<void>(Detail::enqueueEvent(*state, std::move(data)));
        return IO::successStatus();
    }

    IO::Types::Status requestClose(Window &window, Types::Events::CloseRequestSource source) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        static_cast<void>(Detail::requestClose(*state, source));
        return IO::successStatus();
    }

    void applyPresentationPublicationSnapshot(Window &window, const PresentationPublicationSnapshot &snapshot) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr)
            return;
        state->clientSize = snapshot.clientSize;
        state->framebufferSize = snapshot.framebufferSize;
        state->contentScale = snapshot.contentScale;
        state->dpi = snapshot.dpi;
        state->monitor = snapshot.monitor;
        state->presentation = snapshot.presentation;
        state->visible = snapshot.visible;
        state->interactiveMoveResizeActive = snapshot.interactiveMoveResizeActive;
        Detail::publishCachedPresentationState(*state);
        if (Detail::PresentationPublicationState *publication = Detail::WindowAccess::presentationPublication(window))
            publication->publishOccluded(snapshot.occluded);
    }

    std::uint64_t pointerHitMaskGeneration(const Window &window) noexcept
    {
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        return renderer != nullptr ? renderer->pointerHitMaskActiveGeneration : 0;
    }

    std::size_t pointerHitMaskWordCount(const Window &window) noexcept
    {
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        return renderer != nullptr ? renderer->pointerHitMask.size() : 0;
    }

    Types::Renderer::PointerHitMaskWord pointerHitMaskWord(const Window &window, std::size_t index) noexcept
    {
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        return renderer != nullptr && index < renderer->pointerHitMask.size() ? renderer->pointerHitMask[index]
                                                                              : Types::Renderer::PointerHitMaskWord{0};
    }

    const void *pointerHitMaskStorage(const Window &window) noexcept
    {
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        return renderer != nullptr && !renderer->pointerHitMask.empty() ? renderer->pointerHitMask.data() : nullptr;
    }

    // ------------------------------------------------------------
    // Display color fixtures
    // ------------------------------------------------------------
    Types::Display::ColorInfo makeDisplayColorInfo(Types::Display::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept
    {
        return Detail::Platform::makeDisplayColorInfo(
            monitor,
            {.activeColorSpace = snapshot.activeColorSpace,
             .wideColorGamutSupported = snapshot.wideColorGamutSupported,
             .hdrSupported = snapshot.hdrSupported,
             .hdrEnabled = snapshot.hdrEnabled,
             .bitsPerColorChannel = snapshot.bitsPerColorChannel,
             .minimumLuminanceNits = snapshot.minimumLuminanceNits,
             .maximumLuminanceNits = snapshot.maximumLuminanceNits,
             .maximumFullFrameLuminanceNits = snapshot.maximumFullFrameLuminanceNits,
             .sdrWhiteLevelMilli80Nits = snapshot.sdrWhiteLevelMilli80Nits});
    }
} // namespace GameWIP::Desktop::TestHooks
#endif
