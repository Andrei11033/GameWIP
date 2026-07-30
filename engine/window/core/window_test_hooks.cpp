/// @file window_test_hooks.cpp
/// @brief Source-tree-only deterministic Window hooks.

#include "window/internal/window_test_hooks.h"

#if INTERNAL_WINDOW_TEST_HOOKS
#include "window/internal/window_state.h"

#include <new>

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
            state->eventStorageKind = Types::EventStorageKind::External;
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

    IO::Types::Status enqueue(Window &window, Types::EventData data) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        static_cast<void>(Detail::enqueueEvent(*state, std::move(data)));
        return IO::successStatus();
    }

    IO::Types::Status requestClose(Window &window, Types::CloseRequestSource source) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        static_cast<void>(Detail::requestClose(*state, source));
        return IO::successStatus();
    }
} // namespace GameWIP::Window::TestHooks
#endif
