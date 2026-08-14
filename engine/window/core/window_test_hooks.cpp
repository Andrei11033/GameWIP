/// @file window_test_hooks.cpp
/// @brief Source-tree-only deterministic Window hooks.

#include "window/internal/window_test_hooks.h"

#if WINDOW_INTERNAL_TEST_HOOKS
#include "window/internal/window_platform.h"
#include "window/internal/window_state.h"

#include <new>
#include <thread>
#include <utility>

namespace
{
    thread_local GameWIP::Window::TestHooks::FailurePoint armedFailure = GameWIP::Window::TestHooks::FailurePoint::None;
}

namespace GameWIP::Window::Detail
{
    bool consumeFailure(TestHooks::FailurePoint point) noexcept
    {
        if (armedFailure != point)
            return false;
        armedFailure = TestHooks::FailurePoint::None;
        return true;
    }
} // namespace GameWIP::Window::Detail

namespace GameWIP::Window::TestHooks
{
    void failNext(FailurePoint point) noexcept
    {
        armedFailure = point;
    }

    void resetFailures() noexcept
    {
        armedFailure = FailurePoint::None;
    }

    void enablePointerHitMaskBridge(Window &window) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state != nullptr)
            state->pointerHitMaskBackendSupportedForTesting = true;
    }

    void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state != nullptr && state->pointerHitMaskGeneration != nullptr)
        {
            *state->pointerHitMaskGeneration = generation;
            *state->pointerHitMaskGenerationExhausted = false;
            state->pointerHitMaskTargetGeneration = 0;
        }
    }

    bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state == nullptr || Detail::pointerHitMaskAccepts(*state, position);
    }

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
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr ? state->pointerHitMaskActiveGeneration : 0;
    }

    std::size_t pointerHitMaskWordCount(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr ? state->pointerHitMask.size() : 0;
    }

    Types::Renderer::PointerHitMaskWord pointerHitMaskWord(const Window &window, std::size_t index) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr && index < state->pointerHitMask.size() ? state->pointerHitMask[index] : Types::Renderer::PointerHitMaskWord{0};
    }

    const void *pointerHitMaskStorage(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr && !state->pointerHitMask.empty() ? state->pointerHitMask.data() : nullptr;
    }

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
} // namespace GameWIP::Window::TestHooks
#endif
