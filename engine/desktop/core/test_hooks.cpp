/// @file test_hooks.cpp
/// @brief Source-tree-only deterministic Desktop hooks.

#include "desktop/internal/desktop_test_hooks.h"

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/cursor_state.h"
#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/shell_state.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/cursor.h"
#include "desktop/platform/drag_drop.h"
#include "desktop/platform/shell.h"
#include "desktop/platform/window.h"

#include <atomic>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace
{
    // These thread-local controls keep injected failures deterministic and owner-thread scoped.
    thread_local GameWIP::Desktop::Internal::TestHooks::FailurePoint armedFailure = GameWIP::Desktop::Internal::TestHooks::FailurePoint::None;
    thread_local std::size_t cursorNativeCreationFailureCountdown = std::numeric_limits<std::size_t>::max();
    thread_local std::size_t clipboardPublicationFailureIndex = std::numeric_limits<std::size_t>::max();
    thread_local std::size_t clipboardEnumerationFailureCount = std::numeric_limits<std::size_t>::max();
    thread_local std::size_t dragDropRevocationFailures = 0;
    std::atomic_size_t customCursorsCreated = 0;
    std::atomic_size_t customCursorsDestroyed = 0;
} // namespace

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Injected failures and counters
    /// ------------------------------------------------------------

    bool consumeFailure(TestHooks::FailurePoint point) noexcept
    {
        if(armedFailure != point)
        {
            return false;
        }
        armedFailure = TestHooks::FailurePoint::None;
        return true;
    }

    bool consumeCursorNativeCreationFailure() noexcept
    {
        if(cursorNativeCreationFailureCountdown == std::numeric_limits<std::size_t>::max())
        {
            return false;
        }

        if(cursorNativeCreationFailureCountdown != 0)
        {
            --cursorNativeCreationFailureCountdown;
            return false;
        }
        cursorNativeCreationFailureCountdown = std::numeric_limits<std::size_t>::max();
        return true;
    }

    bool consumeClipboardPublicationFailure(std::size_t itemIndex) noexcept
    {
        if(clipboardPublicationFailureIndex != itemIndex)
        {
            return false;
        }
        clipboardPublicationFailureIndex = std::numeric_limits<std::size_t>::max();
        return true;
    }

    bool consumeClipboardEnumerationFailure(std::size_t materializedFormats) noexcept
    {
        if(clipboardEnumerationFailureCount != materializedFormats)
        {
            return false;
        }
        clipboardEnumerationFailureCount = std::numeric_limits<std::size_t>::max();
        return true;
    }

    bool consumeDragDropRevocationFailure() noexcept
    {
        if(dragDropRevocationFailures == 0)
        {
            return false;
        }
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
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop::Internal::TestHooks
{
    /// ------------------------------------------------------------
    /// Failure controls
    /// ------------------------------------------------------------

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

    /// ------------------------------------------------------------
    /// DragDrop policy and lifecycle controls
    /// ------------------------------------------------------------

    Types::DragDrop::Effect negotiateDragDropEffect(
        Types::DragDrop::Effect source,
        Types::DragDrop::Effect target,
        Types::DragDrop::Effect preferred) noexcept
    {
        return Internal::negotiateDragDropEffect(source, target, preferred);
    }

    IO::Types::Status enqueueDragDrop(DragDropTarget &target, Types::DragDrop::Events::Payload data, bool terminal) noexcept
    {
        Internal::DragDropState *state = Internal::DragDropAccess::state(target);

        if(!state)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        return Internal::enqueueDragDropEvent(*state, std::move(data), terminal) ? IO::successStatus()
                                                                                 : IO::makeStatus(IO::Types::ErrorCode::StorageFull);
    }

    Types::DragDrop::SessionId nextDragDropSessionId(DragDropTarget &target, std::uint64_t nextValue) noexcept
    {
        Internal::DragDropState *state = Internal::DragDropAccess::state(target);

        if(state == nullptr)
        {
            return {};
        }
        state->nextSessionId = nextValue;
        return Internal::allocateDragDropSessionId(*state);
    }

    void failDragDropRevocations(std::size_t attempts) noexcept
    {
        dragDropRevocationFailures = attempts;
    }

    std::size_t dragDropRegionCount(const DragDropTarget &target) noexcept
    {
        const Internal::DragDropState *state = Internal::DragDropAccess::state(target);
        return state == nullptr ? 0 : state->regions.size();
    }

    /// ------------------------------------------------------------
    /// Cursor and Clipboard controls
    /// ------------------------------------------------------------

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
        const auto &state = Internal::CursorAccess::state(cursor);
        return state ? state->variants.size() : 0;
    }

    std::uint32_t customCursorBindingDpi(const Window &window) noexcept
    {
        const Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform || !Platform::hasCustomCursor(*state))
        {
            return 0;
        }
        return Platform::customCursorBindingDpi(*state);
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
        const auto &state = Internal::CursorAccess::state(cursor);

        if(!state || index >= state->variants.size())
        {
            return {};
        }
        const Platform::NativeCursorSnapshot snapshot = Platform::inspectNativeCursor(state->variants[index]);
        return {{snapshot.hotspotX, snapshot.hotspotY}, snapshot.firstBgraPixel, snapshot.valid};
    }

    /// ------------------------------------------------------------
    /// Renderer bridge controls
    /// ------------------------------------------------------------

    void enablePointerHitMaskBridge(Window &window) noexcept
    {
        try
        {
            if(Internal::WindowAccess::state(window) != nullptr)
            {
                Internal::WindowAccess::ensureRendererIntegration(window)->pointerHitMaskBackendSupportedForTesting = true;
            }
        }
        catch(const std::bad_alloc &)
        {
            return;
        }
    }

    bool hasRendererIntegrationState(const Window &window) noexcept
    {
        return Internal::WindowAccess::rendererIntegration(window) != nullptr;
    }

    const void *presentationPublicationStorage(const Window &window) noexcept
    {
        return Internal::WindowAccess::presentationPublication(window);
    }

    void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept
    {
        Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);

        if(renderer != nullptr)
        {
            renderer->pointerHitMaskGeneration = generation;
            renderer->pointerHitMaskGenerationExhausted = false;
            renderer->pointerHitMaskTargetGeneration = 0;
        }
    }

    bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept
    {
        const Internal::WindowState *state = Internal::WindowAccess::state(window);
        return state == nullptr || Internal::pointerHitMaskAccepts(*state, position);
    }

    /// ------------------------------------------------------------
    /// Portable lifecycle and events
    /// ------------------------------------------------------------

    IO::Types::Status openPortable(Window &window, std::span<Types::Event> storage) noexcept
    {
        // Validate the wrapper and caller-owned event storage.
        if(Internal::WindowAccess::state(window) != nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        }

        if(storage.empty())
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }

        try
        {
            // Build state and bind stable storage before publishing it to the wrapper.
            auto state = std::make_unique<Internal::WindowState>();
            state->ownerThread = std::this_thread::get_id();
            state->id = {1};
            state->eventStorage = storage;
            state->eventStorageKind = Types::Events::StorageKind::External;
            Internal::WindowAccess::bindRendererIntegration(window, *state);

            if(Internal::WindowAccess::presentationPublication(window) != nullptr)
            {
                Internal::WindowAccess::bindPresentationPublication(window, *state);
                Internal::publishCachedPresentationState(*state);
            }

            Internal::WindowAccess::stateOwner(window) = std::move(state);
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    IO::Types::Status enqueue(Window &window, Types::Events::Payload data) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        static_cast<void>(Internal::enqueueEvent(*state, std::move(data)));
        return IO::successStatus();
    }

    IO::Types::Status enqueue(ChildSurface &surface, Types::ChildSurface::Events::Payload data) noexcept
    {
        Internal::ChildSurfaceState *state = Internal::ChildSurfaceAccess::state(surface);

        if(state == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        static_cast<void>(Internal::enqueueChildSurfaceEvent(*state, std::move(data)));
        return IO::successStatus();
    }

    IO::Types::Status requestClose(Window &window, Types::Events::CloseRequestSource source) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        static_cast<void>(Internal::requestClose(*state, source));
        return IO::successStatus();
    }

    void applyPresentationPublicationSnapshot(Window &window, const PresentationPublicationSnapshot &snapshot) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr)
        {
            return;
        }
        state->clientSize = snapshot.clientSize;
        state->framebufferSize = snapshot.framebufferSize;
        state->contentScale = snapshot.contentScale;
        state->dpi = snapshot.dpi;
        state->monitor = snapshot.monitor;
        state->presentation = snapshot.presentation;
        state->visible = snapshot.visible;
        state->interactiveMoveResizeActive = snapshot.interactiveMoveResizeActive;
        Internal::publishCachedPresentationState(*state);

        if(Internal::PresentationPublicationState *publication = Internal::WindowAccess::presentationPublication(window))
        {
            publication->publishOccluded(snapshot.occluded);
        }
    }

    std::uint64_t pointerHitMaskGeneration(const Window &window) noexcept
    {
        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        return renderer != nullptr ? renderer->pointerHitMaskActiveGeneration : 0;
    }

    std::size_t pointerHitMaskWordCount(const Window &window) noexcept
    {
        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        return renderer != nullptr ? renderer->pointerHitMask.size() : 0;
    }

    Types::Renderer::PointerHitMaskWord pointerHitMaskWord(const Window &window, std::size_t index) noexcept
    {
        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        return renderer != nullptr && index < renderer->pointerHitMask.size() ? renderer->pointerHitMask[index]
                                                                              : Types::Renderer::PointerHitMaskWord{0};
    }

    const void *pointerHitMaskStorage(const Window &window) noexcept
    {
        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        return renderer != nullptr && !renderer->pointerHitMask.empty() ? renderer->pointerHitMask.data() : nullptr;
    }

    /// ------------------------------------------------------------
    /// Display color fixtures
    /// ------------------------------------------------------------

    Types::Display::ColorInfo makeDisplayColorInfo(Types::Display::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept
    {
        return Platform::makeDisplayColorInfo(
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

    /// ------------------------------------------------------------
    /// Shell event hooks
    /// ------------------------------------------------------------

    /// @brief Publishes one synthetic shell event through the queue's normal fixed-capacity path.
    void enqueueShellEvent(ShellEventQueue &queue, Types::Shell::Events::Payload payload) noexcept
    {
        Internal::ShellEventQueueState *state = Internal::ShellEventQueueAccess::state(queue);

        if(state != nullptr)
        {
            Internal::publishShellEvent(*state, std::move(payload));
        }
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
