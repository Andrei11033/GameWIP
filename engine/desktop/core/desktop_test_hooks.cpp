/// @file desktop_test_hooks.cpp
/// @brief Source-tree-only deterministic Window hooks.

#include "desktop/internal/desktop_test_hooks.h"

#if DESKTOP_INTERNAL_TEST_HOOKS
#include "desktop/internal/cursor_platform.h"
#include "desktop/internal/cursor_state.h"
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
    // Failure and cursor controls
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
    }

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
